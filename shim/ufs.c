/*
 * Milestone 5 probe: can we talk to the internal UFS that ABL booted from?
 *
 * Runs with the MMU off (all memory Device, so DMA is coherent with no cache
 * maintenance). Dumps the UFSHCI registers ABL left behind and, if the host
 * is enabled and the link is up, points the transfer request list at our own
 * buffers and issues READ-ONLY SCSI commands on LUN 0. Nothing is ever
 * written to the device: there is no SCSI WRITE in this file.
 *
 * Register layout and descriptor formats: JEDEC UFSHCI (JESD223).
 */
#include <stdint.h>
#include "console.h"
#include "ufs.h"

#define UFS_BASE	0x01d84000UL	/* ufshc@1d84000, kunzite live FDT */

#define REG_CAP		0x00
#define REG_VER		0x08
#define REG_IS		0x20
#define REG_HCS		0x30
#define REG_HCE		0x34
#define REG_UTRLBA	0x50
#define REG_UTRLBAU	0x54
#define REG_UTRLDBR	0x58
#define REG_UTRLCLR	0x5c
#define REG_UTRLRSR	0x60

#define HCS_DP		(1u << 0)
#define HCS_UTRLRDY	(1u << 1)
#define HCS_UCRDY	(1u << 3)

#define BLOCK		4096u

static inline uint32_t ufs_rd(uint32_t off) { return *(volatile uint32_t *)(UFS_BASE + off); }
static inline void ufs_wr(uint32_t off, uint32_t v) { *(volatile uint32_t *)(UFS_BASE + off) = v; }

/* UTP transfer request descriptor (32 bytes). */
struct utrd {
	uint32_t dw0;		/* [31:28] CT, [26:25] DD, [24] I */
	uint32_t dw1;
	uint32_t dw2;		/* [7:0] OCS */
	uint32_t dw3;
	uint32_t ucdba;		/* command descriptor base, 128-byte aligned */
	uint32_t ucdbau;
	uint32_t resp;		/* [31:16] response UPIU offset, [15:0] length (DWORDs) */
	uint32_t prdt;		/* [31:16] PRDT offset, [15:0] entries (DWORDs/entries) */
};

/* UTP command descriptor: command UPIU, response UPIU, PRDT. */
struct ucd {
	uint8_t cmd[512];
	uint8_t resp[512];
	struct {
		uint32_t addr_lo, addr_hi, rsvd, dbc;	/* dbc = byte count - 1 */
	} prdt[1];
};

static struct utrd utrl[32] __attribute__((aligned(1024)));
static struct ucd ucd __attribute__((aligned(128)));
static uint8_t data[2 * BLOCK] __attribute__((aligned(4096)));

static uint64_t ticks(void)
{
	uint64_t t;
	__asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(t));
	return t;
}

static void zero(void *p, unsigned n)
{
	volatile uint8_t *b = p;
	for (unsigned i = 0; i < n; i++)
		b[i] = 0;
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/*
 * Issue one SCSI command (read direction or no data) in slot 0.
 * Returns 0 on success, otherwise a code describing the failure stage.
 */
static uint32_t scsi(uint8_t lun, const uint8_t *cdb, unsigned cdb_len, void *buf, uint32_t len,
    uint32_t *ocs_out, uint32_t *status_out)
{
	static uint8_t tag;
	zero(&utrl[0], sizeof(utrl[0]));
	zero(&ucd, sizeof(ucd));

	/* Command UPIU: header (12 bytes), expected length, CDB. */
	ucd.cmd[0] = 0x01;			/* transaction type: COMMAND */
	ucd.cmd[1] = len ? 0x40 : 0x00;		/* flags: R (data in) */
	ucd.cmd[2] = lun;
	ucd.cmd[3] = ++tag;
	ucd.cmd[12] = len >> 24;
	ucd.cmd[13] = len >> 16;
	ucd.cmd[14] = len >> 8;
	ucd.cmd[15] = len;
	for (unsigned i = 0; i < cdb_len; i++)
		ucd.cmd[16 + i] = cdb[i];

	uintptr_t ucd_pa = (uintptr_t)&ucd;
	utrl[0].dw0 = (1u << 28) | ((len ? 2u : 0u) << 25) | (1u << 24);	/* UFS storage, dev->host */
	utrl[0].dw2 = 0x0f;			/* OCS: invalid until the host updates it */
	utrl[0].ucdba = (uint32_t)ucd_pa;
	utrl[0].ucdbau = (uint32_t)(ucd_pa >> 32);
	utrl[0].resp = (512u / 4) << 16 | (512u / 4);
	if (len) {
		uintptr_t buf_pa = (uintptr_t)buf;
		ucd.prdt[0].addr_lo = (uint32_t)buf_pa;
		ucd.prdt[0].addr_hi = (uint32_t)(buf_pa >> 32);
		ucd.prdt[0].dbc = len - 1;
		utrl[0].prdt = (1024u / 4) << 16 | 1;
	}
	__asm__ volatile("dsb sy" ::: "memory");

	ufs_wr(REG_UTRLDBR, 1);
	uint64_t start = ticks();
	while (ufs_rd(REG_UTRLDBR) & 1) {
		if (ticks() - start > 19200000) {	/* 1 s */
			ufs_wr(REG_UTRLCLR, 1);		/* doorbell bits are write-1-to-clear */
			return 1;			/* timeout */
		}
	}
	__asm__ volatile("dsb sy" ::: "memory");
	*ocs_out = utrl[0].dw2 & 0xff;
	*status_out = ucd.resp[7];		/* SCSI status in the response UPIU */
	/* Callers print: the sense is at resp[32..] (length BE16, then fixed
	 * format data: key at 36, ASC/ASCQ at 46/47), see last_sense(). */
	if (*ocs_out)
		return 2;
	if (ucd.resp[0] != 0x21)		/* RESPONSE UPIU */
		return 3;
	return *status_out ? 4 : 0;
}

static uint32_t read_blocks(uint32_t lba, uint32_t count, void *buf)
{
	uint8_t cdb[10] = { 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, count >> 8, count, 0 };
	uint32_t ocs, st;
	return scsi(0, cdb, sizeof(cdb), buf, count * BLOCK, &ocs, &st);
}

static uint64_t le64(const uint8_t *p)
{
	uint64_t v = 0;
	for (int i = 7; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}

/* Name the filesystem whose superblock starts at block lba (f2fs/ext4). */
static const char *fs_type(uint32_t lba)
{
	if (read_blocks(lba, 1, data))
		return "read error";
	const uint8_t *sb = data + 1024;
	if (sb[0] == 0x10 && sb[1] == 0x20 && sb[2] == 0xf5 && sb[3] == 0xf2)
		return "f2fs";
	if (sb[56] == 0x53 && sb[57] == 0xef)
		return "ext4";
	return "other";
}

/* One line per GPT entry of at least 64 MiB: name, first LBA, size in MiB (4K blocks). */
static void list_partitions(uint32_t entry_lba, uint32_t nent)
{
	static uint8_t ents[128 * 128] __attribute__((aligned(4096)));
	uint32_t blocks = (nent * 128 + BLOCK - 1) / BLOCK;
	if (read_blocks(entry_lba, blocks, ents)) {
		con_str("UFS PART", "read error");
		return;
	}
	for (uint32_t i = 0; i < nent; i++) {
		const uint8_t *e = ents + i * 128;
		uint64_t first = le64(e + 32), last = le64(e + 40);
		if (!first)
			continue;
		char name[17];
		for (int c = 0; c < 16; c++) {
			uint8_t ch = e[56 + 2 * c];
			name[c] = (ch >= 32 && ch < 127) ? ch : (ch ? '?' : 0);
		}
		name[16] = 0;
		if (((last - first + 1) >> 8) < 64)	/* keep the screen to the big ones */
			continue;
		con_hex2(name, "lba=", (uint32_t)first, "MiB=", (uint32_t)((last - first + 1) >> 8));
		if (!__builtin_strcmp(name, "userdata") || !__builtin_strcmp(name, "cust") ||
		    !__builtin_strcmp(name, "cache"))
			con_str(" fs", fs_type((uint32_t)first));
	}
}

/* ================= kunzite UFS experiment matrix (2026-10-20) =================
 * One boot answers the synthesis matrix: CFG1 SCSI-check bit, response-UPIU
 * forensics with CORRECT sense offsets, NOP OUT, the query ladder (device
 * descriptor, LUN descriptor, fDeviceInit), then TUR/READ again. The screen
 * holds forever (no reset): photograph it in the morning.
 * Layout notes (Linux ufshcd encodings): dev-manage UTRD DW0 CT=2<<28; query
 * struct sits at UPIU byte 12 (opcode,idn,index,selector,res_osf,len,value..);
 * NOP OUT is a bare 0x00 transaction header. SCSI response sense: length BE16
 * at bytes [32:33], data from 34, key at 36, ASC/ASCQ at 46/47.
 */
#define REG_UFS_CFG1		0xDCu	/* 0x1d84000+0xDC: BIT31 UTP_SCSI_CHECK_DIS */
#define CT_DEV_MANAGE		(2u << 28)

static uint8_t dm_tag;

/* One device-manage UPIU on slot 0 (no data buffers; responses stay in the
 * 512-byte response area). Returns 0 on OCS=0 completion. */
static uint32_t dm_upiu(uint8_t trans, uint8_t fn, uint8_t dd, const uint8_t q[16])
{
	zero(&utrl[0], sizeof(utrl[0]));
	zero(&ucd, sizeof(ucd));
	ucd.cmd[0] = trans;
	ucd.cmd[3] = ++dm_tag;
	ucd.cmd[5] = fn;
	if (q != 0) {
		for (int i = 0; i < 16; i++)
			ucd.cmd[12 + i] = q[i];
	}
	uintptr_t ucd_pa = (uintptr_t)&ucd;
	utrl[0].dw0 = CT_DEV_MANAGE | ((uint32_t)dd << 25) | (1u << 24);
	utrl[0].dw2 = 0x0f;
	utrl[0].ucdba = (uint32_t)ucd_pa;
	utrl[0].ucdbau = (uint32_t)(ucd_pa >> 32);
	utrl[0].resp = (512u / 4) << 16 | (512u / 4);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_wr(REG_UTRLDBR, 1);
	uint64_t start = ticks();
	while (ufs_rd(REG_UTRLDBR) & 1) {
		if (ticks() - start > 19200000) {
			ufs_wr(REG_UTRLCLR, 1);	/* write-1-to-clear */
			return 1;
		}
	}
	__asm__ volatile("dsb sy" ::: "memory");
	return utrl[0].dw2 & 0xff;
}

/* Sense of the last SCSI response: key<<16 | ASC<<8 | ASCQ (0 = none). */
static uint32_t last_sense(void)
{
	if (((uint32_t)ucd.resp[32] << 8 | ucd.resp[33]) == 0)
		return 0;
	return (uint32_t)(ucd.resp[36] & 0xf) << 16 | (uint32_t)ucd.resp[46] << 8 | ucd.resp[47];
}

/* One SCSI command = one screen line: rc<<16 | UPIU response<<8 | status, sense. */
static uint32_t scsi_line(const char *label, uint8_t lun, const uint8_t *cdb, unsigned cdb_len,
    void *buf, uint32_t len)
{
	uint32_t ocs = 0, st = 0;
	uint32_t rc = scsi(lun, cdb, cdb_len, buf, len, &ocs, &st);
	con_hex2(label, "rc/rs=", rc << 16 | (uint32_t)ucd.resp[6] << 8 | ucd.resp[7],
	    "k/a=", last_sense());
	return rc;
}

/* One query request (function 0x01 read / 0x81 write). Returns 0 on success,
 * else the query response code, or 0x100|OCS. *value = the BE32 value field
 * (attributes, flags); descriptor data starts at resp+32. */
static uint32_t query(uint8_t fn, uint8_t opcode, uint8_t idn, uint8_t index, uint16_t len,
    uint32_t *value)
{
	uint8_t q[16] = { opcode, idn, index, 0, 0, 0, (uint8_t)(len >> 8), (uint8_t)len };
	uint32_t ocs = dm_upiu(0x16, fn, 0, q);
	if (value != 0)
		*value = be32(ucd.resp + 20);
	return ocs ? 0x100 | ocs : ucd.resp[6];
}

/* bCurrentPowerMode (attr 0x02: 0x11 Active, 0x22 Sleep, 0x33 PowerDown,
 * 0x00 Idle) and bBootLunEn (attr 0x00: 1 = boot LU A, 2 = B). */
static void attr_line(const char *label)
{
	uint32_t pwr = 0, boot = 0;
	uint32_t qp = query(0x01, 0x03, 0x02, 0, 0, &pwr);
	uint32_t qb = query(0x01, 0x03, 0x00, 0, 0, &boot);
	con_hex2(label, "pwr=", qp << 16 | (pwr & 0xffff), "boot=", qb << 16 | (boot & 0xffff));
}

/*
 * v6 (2026-10-20): every LU answers 02/04/02 "initializing command
 * required", even INQUIRY and REPORT LUNS, while NOP and queries work and
 * the device descriptor is sane (UFS 2.2, Samsung, 6 LUs). Hypothesis: ABL
 * leaves the device in UFS-Sleep/PowerDown at handoff; Linux never sees it
 * because ufs-qcom resets the device at probe. UFS sets the device power
 * mode with START STOP UNIT to the UFS Device W-LUN (0xD0), POWER
 * CONDITION in cdb[4] bits 7:4 (Linux ufshcd_set_dev_pwr_mode: Active =
 * 0x10). v4 sent SSU to LUN 0 with the START bit instead. Also: the config
 * descriptor is IDN 0x01 (v5 asked for 0x10 -> INVALID_IDN).
 */
void ufs_matrix(void)
{
	con_str("UFS MAT", "exp matrix v6 (holds)");
	/* CFG1 BIT31 (SCSI check disable) stays set as in v1-v5: one variable at a time */
	uint32_t cfg1 = ufs_rd(REG_UFS_CFG1);
	if (!(cfg1 & (1u << 31)))
		ufs_wr(REG_UFS_CFG1, cfg1 | (1u << 31));
	con_hex2("UFS CFG1", "was=", cfg1, "now=", ufs_rd(REG_UFS_CFG1));

	/* take over the transfer list (validated path) */
	ufs_wr(REG_UTRLRSR, 0);
	uintptr_t list = (uintptr_t)utrl;
	ufs_wr(REG_UTRLBA, (uint32_t)list);
	ufs_wr(REG_UTRLBAU, (uint32_t)(list >> 32));
	ufs_wr(REG_UTRLRSR, 1);
	if (ufs_rd(REG_UTRLBA) != (uint32_t)list) {
		con_str("UFS", "list base not taken");
		return;
	}

	static const uint8_t tur[6] = { 0x00 };
	scsi_line("UFS TUR0", 0, tur, sizeof(tur), 0, 0);	/* baseline: expect 02/04/02 */
	attr_line("UFS ATR0");

	/* REQUEST SENSE on the device W-LUN (PRDT byte counts are dword-sized: 20) */
	static const uint8_t rqs[6] = { 0x03, 0, 0, 0, 20, 0 };
	uint32_t ocs = 0, st = 0;
	uint32_t rc = scsi(0xD0, rqs, sizeof(rqs), data, 20, &ocs, &st);
	con_hex2("UFS RQSW", "rc/rs=", rc << 16 | (uint32_t)ucd.resp[6] << 8 | ucd.resp[7],
	    "k/a=", rc ? last_sense() :
	    (uint32_t)(data[2] & 0xf) << 16 | (uint32_t)data[12] << 8 | data[13]);

	/* THE experiment: SSU to the device W-LUN, power condition Active */
	static const uint8_t ssu_active[6] = { 0x1B, 0, 0, 0, 0x10, 0 };
	for (int i = 0; i < 3; i++) {
		char label[] = "UFS SSU1";
		label[7] = '1' + i;
		if (scsi_line(label, 0xD0, ssu_active, sizeof(ssu_active), 0, 0) == 0)
			break;
	}
	attr_line("UFS ATR1");

	for (int i = 0; i < 3; i++) {
		char label[] = "UFS TUR1";
		label[7] = '1' + i;
		if (scsi_line(label, 0, tur, sizeof(tur), 0, 0) == 0)
			break;
	}

	static const uint8_t inq[6] = { 0x12, 0, 0, 0, 36, 0 };
	if (scsi_line("UFS INQ", 0, inq, sizeof(inq), data, 36) == 0) {
		char id[25];
		for (int i = 0; i < 24; i++) {
			uint8_t ch = data[8 + i];
			id[i] = (ch >= 32 && ch < 127) ? (char)ch : '.';
		}
		id[24] = 0;
		con_str("UFS INQS", id);
	}

	static const uint8_t rd10[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 2, 0 };
	if (scsi_line("UFS READ", 0, rd10, sizeof(rd10), data, 2 * BLOCK) == 0) {
		char sig[9];
		for (int i = 0; i < 8; i++) {
			uint8_t ch = data[BLOCK + i];
			sig[i] = (ch >= 32 && ch < 127) ? (char)ch : '.';
		}
		sig[8] = 0;
		con_str("UFS GPT", sig);
	}

	/* unit descriptors (IDN 0x02): enable/boot-LU/write-protect + block count
	 * low 32 bits (maps LUN -> sdX: sda has 0x07686000 4K blocks) */
	for (uint8_t lun = 0; lun < 6; lun++) {
		char label[] = "UFS LU0";
		label[6] = '0' + lun;
		uint32_t q = query(0x01, 0x01, 0x02, lun, 64, 0);
		const volatile uint8_t *d = ucd.resp + 32;
		con_hex2(label, "q/e/b/w=", q << 24 | (uint32_t)d[3] << 16 | (uint32_t)d[4] << 8 | d[5],
		    "blk=", be32((const uint8_t *)(ucd.resp + 32 + 15)));
	}

	/* configuration descriptor, the right IDN this time (0x01) */
	uint32_t q = query(0x01, 0x01, 0x01, 0, 255, 0);
	con_hex2("UFS CFGD", "q/len=", q << 8 | ucd.resp[32],
	    "boot/ipm=", (uint32_t)ucd.resp[35] << 8 | ucd.resp[37]);

	con_str("UFS MAT", "v6 done - hold");
	for (;;)
		__asm__ volatile("wfe");
}

/* ================= kunzite persistent-log viewer (2026-10-20) =================
 * Boot #15 drew the Apple logo, then iOS rebooted the phone. The kernel's
 * SWEETLOG ring (patch 0047: header + 4 MiB ring in the last 4 MiB + 4 KiB
 * of userdata, flushed every 10 s) holds what happened, but recovery adb is
 * unauthorized now that the Android data is gone. Read it straight from
 * UFS and show the newest ~200 lines: two older pages for 40 s each, then
 * the newest page holds. Read-only (READ(10) only).
 */
static uint8_t logbuf[6 * BLOCK] __attribute__((aligned(4096)));
static uint8_t gptbuf[4 * BLOCK] __attribute__((aligned(4096)));
static struct { const uint8_t *p; int n; } log_rows[3 * 80];

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void log_wait_s(unsigned s)
{
	uint64_t end = ticks() + (uint64_t)19200000 * s;
	while (ticks() < end)
		;
}

void ufs_logview(void)
{
	con_str("LOG", "kunzite log viewer (read-only)");
	ufs_wr(REG_UTRLRSR, 0);
	uintptr_t list = (uintptr_t)utrl;
	ufs_wr(REG_UTRLBA, (uint32_t)list);
	ufs_wr(REG_UTRLBAU, (uint32_t)(list >> 32));
	ufs_wr(REG_UTRLRSR, 1);
	if (ufs_rd(REG_UTRLBA) != (uint32_t)list) {
		con_str("LOG", "list base not taken");
		return;
	}
	/* wake the device (matrix v6): SSU power condition Active to W-LUN 0xD0 */
	static const uint8_t ssu_active[6] = { 0x1B, 0, 0, 0, 0x10, 0 };
	static const uint8_t tur[6] = { 0x00 };
	uint32_t ocs = 0, st = 0, rc = 1;
	for (int i = 0; i < 3 && rc; i++)
		rc = scsi(0xD0, ssu_active, sizeof(ssu_active), 0, 0, &ocs, &st);
	for (int i = 0; i < 3 && (rc = scsi(0, tur, sizeof(tur), 0, 0, &ocs, &st)); i++)
		;
	if (rc) {
		con_hex2("LOG UFS", "tur=", rc, "k/a=", last_sense());
		return;
	}

	/* GPT on LUN 0: header at LBA 1, scan 128 entries (Xiaomi understates the count) */
	if (read_blocks(1, 1, data)) {
		con_str("LOG", "GPT header read failed");
		return;
	}
	uint32_t elba = le32(data + 72);
	if (read_blocks(elba, 4, gptbuf)) {
		con_str("LOG", "GPT entries read failed");
		return;
	}
	uint64_t last = 0;
	for (int i = 0; i < 128 && !last; i++) {
		const uint8_t *e = gptbuf + i * 128;
		static const char want[] = "userdata";
		int ok = 1;
		for (int c = 0; c < 9 && ok; c++)	/* includes the terminating 0 */
			ok = e[56 + 2 * c] == (uint8_t)want[c] && e[57 + 2 * c] == 0;
		if (ok)
			last = le64(e + 40);
	}
	if (!last) {
		con_str("LOG", "no userdata in the GPT");
		return;
	}
	uint32_t area = (uint32_t)(last + 1 - 1025);	/* 1 header + 1024 ring blocks */
	if (read_blocks(area, 1, data)) {
		con_str("LOG", "log header read failed");
		return;
	}
	static const char magic[] = "SWEETLOG";
	for (int i = 0; i < 8; i++) {
		if (data[i] != (uint8_t)magic[i]) {
			con_hex2("LOG", "no SWEETLOG at blk=", area, "b0=", le32(data));
			return;
		}
	}
	uint32_t size = le32(data + 12);
	uint64_t total = le64(data + 16), seq = le64(data + 24);
	con_hex2("LOG HDR", "ring=", size, "flush=", (uint32_t)seq);
	con_hex2("LOG HDR", "total=", (uint32_t)total, "blk=", area);
	if (total == 0 || size == 0 || size % BLOCK) {
		con_str("LOG", "empty or odd ring");
		return;
	}

	/* the last up to 6 ring blocks, in stream order */
	uint32_t nring = size / BLOCK;
	uint32_t lb = (uint32_t)((total - 1) % size), last_blk = lb / BLOCK;
	uint32_t nblk = 6;
	if (total <= size && last_blk + 1 < nblk)
		nblk = last_blk + 1;
	for (uint32_t k = 0; k < nblk; k++) {
		uint32_t b = (last_blk + nring - (nblk - 1) + k) % nring;
		if (read_blocks(area + 1 + b, 1, logbuf + k * BLOCK)) {
			con_hex2("LOG", "ring read failed b=", b, "k=", k);
			return;
		}
	}
	int vend = (int)((nblk - 1) * BLOCK + lb % BLOCK + 1);

	/* display rows, newest first, long lines wrapped */
	int cols = con_text_cols(2), per = con_text_rows(2) - 1, maxrows = 3 * per;
	if (maxrows > (int)(sizeof(log_rows) / sizeof(log_rows[0])))
		maxrows = sizeof(log_rows) / sizeof(log_rows[0]);
	int nrows = 0, end = vend;
	if (end > 0 && logbuf[end - 1] == '\n')
		end--;
	while (end >= 0 && nrows < maxrows) {
		int s0 = end;
		while (s0 > 0 && logbuf[s0 - 1] != '\n')
			s0--;
		int len = end - s0, pieces = len ? (len + cols - 1) / cols : 1;
		for (int k = pieces - 1; k >= 0 && nrows < maxrows; k--) {
			int n = len - k * cols;
			log_rows[nrows].p = logbuf + s0 + k * cols;
			log_rows[nrows].n = n > cols ? cols : n;
			nrows++;
		}
		if (s0 == 0)
			break;
		end = s0 - 1;
	}
	log_wait_s(5);

	/* pages: oldest first (40 s each), the newest one holds */
	int npages = (nrows + per - 1) / per;
	for (int pg = npages - 1; pg >= 0; pg--) {
		con_text_begin(0xff000000U);
		/* literals per page: no copied char arrays (memcpy) or pointer tables (ABS64) */
		if (pg == 0)
			con_text_line("== SWEETLOG newest lines (held) ==", 34, 2, GREEN);
		else if (pg == 1)
			con_text_line("== SWEETLOG older lines (40 s) ==", 33, 2, GREEN);
		else
			con_text_line("== SWEETLOG oldest lines (40 s) ==", 34, 2, GREEN);
		int top = (pg + 1) * per < nrows ? (pg + 1) * per : nrows;
		for (int r = top - 1; r >= pg * per; r--)
			con_text_line((const char *)log_rows[r].p, log_rows[r].n, 2, WHITE);
		if (pg)
			log_wait_s(40);
	}
	for (;;)
		__asm__ volatile("wfe");
}

void test_ufs(void)
{
	con_str("UFS", "milestone 5 probe (read-only)");
	uint32_t cap = ufs_rd(REG_CAP), hcs = ufs_rd(REG_HCS), hce = ufs_rd(REG_HCE);
	con_hex2("UFS", "ver=", ufs_rd(REG_VER), "cap=", cap);
	con_hex2("UFS", "hce=", hce, "hcs=", hcs);
	con_hex2("UFS", "is=", ufs_rd(REG_IS), "rsr=", ufs_rd(REG_UTRLRSR));
	con_hex2("UFS", "utrlba=", ufs_rd(REG_UTRLBA), "hi=", ufs_rd(REG_UTRLBAU));
	con_hex2("UFS", "dbr=", ufs_rd(REG_UTRLDBR), "nutrs=", (cap & 0x1f) + 1);

	if (!(hce & 1) || !(hcs & HCS_DP) || !(hcs & HCS_UTRLRDY)) {
		con_str("UFS", "host not enabled/ready: stop");
		return;
	}
	if (ufs_rd(REG_UTRLDBR)) {
		con_str("UFS", "requests outstanding: stop");
		return;
	}

	/* Take over the transfer request list: stop, point at ours, run. */
	ufs_wr(REG_UTRLRSR, 0);
	uintptr_t list = (uintptr_t)utrl;
	ufs_wr(REG_UTRLBA, (uint32_t)list);
	ufs_wr(REG_UTRLBAU, (uint32_t)(list >> 32));
	ufs_wr(REG_UTRLRSR, 1);
	con_hex2("UFS", "ours=", (uint32_t)list, "rsr=", ufs_rd(REG_UTRLRSR));
	/*
	 * Never ring a doorbell unless the host really uses our list: otherwise
	 * it would execute whatever ABL left in its own slot 0 (maybe a write).
	 */
	if (ufs_rd(REG_UTRLBA) != (uint32_t)list || ufs_rd(REG_UTRLBAU) != (uint32_t)(list >> 32) ||
	    !(ufs_rd(REG_UTRLRSR) & 1)) {
		con_str("UFS", "list base not taken: stop");
		return;
	}

	uint32_t ocs = 0, st = 0, rc;
	static const uint8_t tur[6] = { 0x00 };
	/* A first command may report a unit attention; retry a few times. */
	for (int i = 0; i < 3; i++) {
		rc = scsi(0, tur, sizeof(tur), 0, 0, &ocs, &st);
		con_hex2("UFS TUR", "rc=", rc, "ocs/st=", ocs << 8 | st);
		if (!rc)
			break;
	}

	static const uint8_t cap10[10] = { 0x25 };	/* READ CAPACITY(10) */

	/*
	 * kunzite has 6 LUNs (sda..sdf) and the boot GPT with userdata is on one
	 * of them: sweep LUN 0..5 with TUR so the screen names the live LUNs and
	 * their sense codes (which LUN the kernel driver must use).
	 */
	static const uint8_t tur6[6] = { 0 };
	uint8_t good_lun = 0xff;
	for (uint8_t lun = 0; lun < 6; lun++) {
		char label[] = "UFS LN0";
		label[6] = '0' + lun;
		rc = scsi(lun, tur6, sizeof(tur6), 0, 0, &ocs, &st);
		con_hex2(label, "rc=", rc, "ocs/st=", ocs << 8 | st);
		if (!rc && good_lun == 0xff)
			good_lun = lun;
	}
	if (good_lun != 0xff) {
		rc = scsi(good_lun, cap10, sizeof(cap10), data, 8, &ocs, &st);
		con_hex2("UFS CAP", "rc=", rc, "ocs/st=", ocs << 8 | st);
		if (!rc)
			con_hex2("UFS LUNx", "lun=", good_lun, "blk=", be32(data + 4));
	}

	rc = scsi(0, cap10, sizeof(cap10), data, 8, &ocs, &st);
	con_hex2("UFS CAP", "rc=", rc, "ocs/st=", ocs << 8 | st);
	if (!rc)
		con_hex2("UFS LUN0", "lastlba=", be32(data), "blk=", be32(data + 4));

	/* READ(10) LBA 0, 2 blocks: protective MBR and the GPT header. */
	static const uint8_t rd10[10] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 2, 0 };
	rc = scsi(0, rd10, sizeof(rd10), data, 2 * BLOCK, &ocs, &st);
	con_hex2("UFS READ", "rc=", rc, "ocs/st=", ocs << 8 | st);
	if (rc)
		return;
	con_hex2("UFS MBR", "sig=", data[510] | data[511] << 8, "type=", data[450]);
	const uint8_t *gpt = data + BLOCK;
	char sig[9];
	for (int i = 0; i < 8; i++)
		sig[i] = (gpt[i] >= 32 && gpt[i] < 127) ? gpt[i] : '.';
	sig[8] = 0;
	con_str("UFS GPT", sig);
	uint32_t nent = gpt[80] | gpt[81] << 8 | gpt[82] << 16, esz = gpt[84] | gpt[85] << 8;
	uint32_t elba = gpt[72] | gpt[73] << 8;
	con_hex2("UFS GPT", "entries=", nent, "lba=", elba);
	if (esz != 128 || nent > 128)
		return;
	list_partitions(elba, nent);
}
