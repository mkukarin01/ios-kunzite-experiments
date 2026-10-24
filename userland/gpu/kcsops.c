/*
 * kcsops NAME|PID...: what the kernel says of a running process's code signature, as tccd and
 * the other entitlement checkers ask it (csops): the status flags, the signing identity, and the
 * entitlements, both as the XML blob (CS_OPS_ENTITLEMENTS_BLOB) and as DER
 * (CS_OPS_DER_ENTITLEMENTS_BLOB, what iOS 16's CoreEntitlements reads), each with its size and
 * whether it names com.apple.private.tcc.allow and kTCCServicePhotos. A NAME is every process
 * of that name. kcsops -w SECONDS NAME waits for a process of that name to start (polling every
 * 20 ms) and shows it at once: for one that dies within a second (assetsd here).
 * For the scouting of assetsd's "assetsd requires TCCPhotosService access".
 * Built by mk-gl-triangle.sh.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* sys/codesign.h and libproc.h are not in the iOS SDK */
extern int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
extern int proc_listallpids(void *buffer, int buffersize);
extern int proc_name(int pid, void *buffer, uint32_t buffersize);

#define CS_OPS_STATUS                   0
#define CS_OPS_ENTITLEMENTS_BLOB        7
#define CS_OPS_IDENTITY                 11
#define CS_OPS_DER_ENTITLEMENTS_BLOB    16

static const struct { uint32_t bit; const char *name; } flags[] = {
	{ 0x00000001, "VALID" }, { 0x00000002, "ADHOC" }, { 0x00000004, "GET_TASK_ALLOW" },
	{ 0x00000008, "INSTALLER" }, { 0x00000100, "HARD" }, { 0x00000200, "KILL" },
	{ 0x00000800, "RESTRICT" }, { 0x00001000, "ENFORCEMENT" }, { 0x00002000, "REQUIRE_LV" },
	{ 0x00004000, "ENTITLEMENTS_VALIDATED" }, { 0x00008000, "NVRAM_UNRESTRICTED" },
	{ 0x00010000, "RUNTIME" }, { 0x04000000, "PLATFORM_BINARY" }, { 0x08000000, "PLATFORM_PATH" },
	{ 0x10000000, "DEBUGGED" }, { 0x20000000, "SIGNED" }, { 0x40000000, "DEV_CODE" },
	{ 0x80000000, "DATAVAULT_CONTROLLER" },
};

static int
has(const unsigned char *data, size_t size, const char *text)
{
	size_t n = strlen(text);

	for (size_t i = 0; i + n <= size; i++) {
		if (memcmp(data + i, text, n) == 0) {
			return 1;
		}
	}
	return 0;
}

/* a blob: 8-byte header (magic, length, big-endian), then the entitlements */
static void
blob(pid_t pid, unsigned int op, const char *what)
{
	static unsigned char buffer[256 * 1024];
	int r;

	memset(buffer, 0, 8);
	r = csops(pid, op, buffer, sizeof(buffer));
	if (r != 0) {
		printf("  %s: csops error %d (%s)\n", what, errno, strerror(errno));
		return;
	}
	uint32_t magic = (uint32_t)buffer[0] << 24 | (uint32_t)buffer[1] << 16 | (uint32_t)buffer[2] << 8 | buffer[3];
	uint32_t length = (uint32_t)buffer[4] << 24 | (uint32_t)buffer[5] << 16 | (uint32_t)buffer[6] << 8 | buffer[7];
	size_t body = length > 8 && length <= sizeof(buffer) ? length - 8 : 0;
	printf("  %s: magic %#x, %u bytes; tcc.allow %s, kTCCServicePhotos %s\n", what, magic, length,
	    has(buffer + 8, body, "com.apple.private.tcc.allow") ? "yes" : "no",
	    has(buffer + 8, body, "kTCCServicePhotos") ? "yes" : "no");
}

static void
show(pid_t pid, const char *name)
{
	uint32_t status = 0;
	char identity[512] = "";

	printf("%s (pid %d)\n", name, pid);
	if (csops(pid, CS_OPS_STATUS, &status, sizeof(status)) != 0) {
		printf("  status: csops error %d (%s)\n", errno, strerror(errno));
	} else {
		printf("  status %#010x:", status);
		for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
			if (status & flags[i].bit) {
				printf(" %s", flags[i].name);
			}
		}
		printf("\n");
	}
	if (csops(pid, CS_OPS_IDENTITY, identity, sizeof(identity) - 1) == 0) {
		printf("  identity: %s\n", identity[8] != 0 ? identity + 8 : "(empty)");
	} else {
		printf("  identity: csops error %d (%s)\n", errno, strerror(errno));
	}
	blob(pid, CS_OPS_ENTITLEMENTS_BLOB, "entitlements (XML)");
	blob(pid, CS_OPS_DER_ENTITLEMENTS_BLOB, "entitlements (DER)");
}

int
main(int argc, char **argv)
{
	static pid_t pids[8192];
	int count;

	if (argc == 4 && strcmp(argv[1], "-w") == 0) {
		for (long tries = strtol(argv[2], NULL, 10) * 50; tries > 0; tries--) {
			count = proc_listallpids(pids, sizeof(pids));
			for (int i = 0; i < count; i++) {
				char name[256] = "";
				if (proc_name(pids[i], name, sizeof(name)) > 0 && strcmp(name, argv[3]) == 0) {
					show(pids[i], name);
					return 0;
				}
			}
			usleep(20000);
		}
		printf("%s: did not start in %s s\n", argv[3], argv[2]);
		return 1;
	}
	count = proc_listallpids(pids, sizeof(pids));
	for (int a = 1; a < argc; a++) {
		char *end;
		long pid = strtol(argv[a], &end, 10);
		unsigned found = 0;

		if (*end == 0) {
			char name[256] = "";
			proc_name((int)pid, name, sizeof(name));
			show((pid_t)pid, name[0] ? name : "?");
			continue;
		}
		for (int i = 0; i < count; i++) {
			char name[256] = "";
			if (proc_name(pids[i], name, sizeof(name)) > 0 && strcmp(name, argv[a]) == 0) {
				show(pids[i], name);
				found++;
			}
		}
		if (found == 0) {
			printf("%s: not running\n", argv[a]);
		}
	}
	return 0;
}
