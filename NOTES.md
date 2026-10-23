# iOS 16 on sweet (Redmi Note 10 Pro, SM7150) - working notes

## >>> HANDOFF (2026-10-23) - branch kunzite-topaz-gpu - READ FIRST <<<

**State:** `kunzite-topaz-gpu` = kunzite 39b53a5 + upstream topaz e477291 (the HackiOS batch),
merge 6cdf249, our patches 0960-0969, build fix 54b61da. **CI run 39923346510 (54b61da)
SUCCEEDED** (xnu-kernel artifact 61 MB). NOT booted yet: the last booted kernel is still d5f0c370
(boot #24). The previous run 39919704364 failed on a hunk lost when 0908 became 0967 (details in
the merge notes below).

**Boot #25 (next) = `shim/out/shim-boot-25-gpu.img`** (built 2026-10-23, sha256 2f81806b...).
It is shim-boot-25.img with only the kernel swapped. Kernel = CI 39923346510 artifact (sha256
3f103d50..., SHA256SUMS OK; has kunzite_touch_init + topaz_adreno_init). ADT = the same
shim/out/kunzite.adt (/pram). Cmdline = the standard one + `sweetusb=2`. payload-25-gpu.bin was
built with mkpayload.py, the image with shim/build-mac.sh (shim/ and tools/ unchanged since
69446d0). Checked: the shim bytes equal shim-boot-25.img's except the image_size field, and the
embedded payload equals payload-25-gpu.bin. The old shim-boot-25.img (kernel d5f0c370, never
booted) is kept. Boot: `fastboot boot shim/out/shim-boot-25-gpu.img` from the BOOTLOADER
fastboot, then `tools/kunzite-usbnet-mac.sh` + `tools/udp-log.py FILE` (0969 replay is in this kernel).

**BOOT #25 (2026-10-23 14:37, shim-boot-25-gpu.img): BOOTS, works like #24, one NEW crash loop.**
Log: temp/ios-m8/usblog/live-boot25-20261023-143802.txt (full replay from kernel start). Seen:
`SWEET FB: modes published: 1125x2436 at 60 Hz` (0948), `SWEET BATTERY: static fallback` (0148
dormant), CPUFREQ read-only, GT9916K touch (25 touch events), keys, display off ignored under
stayon, Spotlight + Settings (Preferences) opened, agent up (pid 89), ue0 rx/tx fine. No panic.
**New: syncdefaultsd (com.apple.kvsd) aborts on every launch** (12x in the log, 14 .ips on the
phone; none in #24, where it ran once). Copies: temp/ios-m8/phone-facts/crash-boot25/ (2 .ips +
the binary). Cause, confirmed by disassembly (syncdefaultsd 0x10001cf0c-0x10001cf44): it calls
sandbox_container_path_for_audit_token(token, buf, 0x400) on an UNINITIALISED stack buffer and
only skips the client if the call fails or buf[0]==0. Otherwise it does
+[NSURL fileURLWithPath:isDirectory:YES] -> NSInvalidArgumentException -> abort. Upstream 0936
(topaz_sandbox_container.c, sandbox op 4) returns 0 WITHOUT writing the buffer when the process
has no CFFIXED_USER_HOME (most daemons): the not-found fall-through (`error` is still 0 from
copyin) and the early `return 0` exits. Before the merge op 4 was always ENOENT (0149 weak
default) -> clean skip. **Fix = 0970-kunzite-sandbox-container-enoent.patch** (2026-10-23): op 4
returns ENOENT on every no-answer path. Applied in series order (only 0936 touches the file),
the result matches the edit; xnu/patches now holds 212 patches. NOT compiled locally (no swiftlang clang, no CI log for xnu-cc.py) and NOT built by CI yet.
Probably an upstream topaz bug too. Any other sandbox_container_path_* caller gets
the same garbage "success", so the fix covers more than syncdefaultsd.
Second victim (16:08, 2x, also absent in #24): **webbookmarksd** aborts in the same
+[NSURL fileURLWithPath:isDirectory:YES], in its passwordIconsRepeatingCleanup XPC activity. arm64
slice 0x10000b9cc-0x10000b9e4: the path is _SFSafariIconImageCacheDirectoryPath(), derived from
_SFSafariContainerPath; the binary imports sandbox_container_path_for_pid. Very likely the same
cause; not traced inside the dyld-cache framework. Copies are in the same crash-boot25/ dir.

**BOOT #26 (2026-10-23 15:55, shim/out/shim-boot-26.img = #25 shim + ADT + cmdline, kernel CI
39941218932 @ aca67b0 sha256 87e73ec0..., 0970 confirmed in the binary: every no-answer exit
loads w21=2 before returning): 0970 WORKS.** Log: temp/ios-m8/usblog/live-boot26-20261023-1556.txt
(starts mid-boot; a manual "replay" request appended the kernel start from line ~2184; a
listener started BEFORE fastboot boot misses the replay, because its requests go out before ue0
is up). In 12+ min: syncdefaultsd and webbookmarksd ran without a crash (agent /ps: still their
first pids, 115 and 225; #25 had 12 syncdefaultsd aborts within 2 min and the first
webbookmarksd one at ~5 min). Only new report: **Preferences SIGSEGV at pc 0** in Settings ->
Notifications (crash-boot26/): MobileIcons -> +[CIContext _MI_sharedIconCompositorContext] ->
internalContextWithEAGLContext -> CI::GLContext::GLContext calls a NULL GL entry point. That is
CoreImage taking the OpenGL ES road with no GL behind it (no GPU, empty IOAcceleratorES 0138).
Not container-path related. Unknown whether pre-merge kernels crash the same way; untested.
The GPU port (or HackiOS's eagl) is the real fix. Report counter suffixes (.000N) restart each
boot, and the epoch clock repeats (16:03 = launchd start every boot): tell boots apart by
file set, not by name.

**GPU PROBE 0971 (2026-10-23, written, NOT built/booted): `kgpu=N`, read-only steps 1-3** of the
port map (main repo ios/gpu-plan.md). New bsd/dev/kunzite_adreno.c (+ files entry, hook +
extern in sweet_ufs.c after sweet_usb_probe). Thread waits kgpu_delay s (default 60), then each
group of reads is announced ("SWEET KGPU: N about to read ...") and kgpu_pause ms (default
1500) pass before the reads. Step 1: GCC gpu_cfg_ahb 0x81004 / memnoc_gfx 0x81010 /
snoc_dvm_gfx 0x81018 / GPLL0 votes 0x62000, cmd-db ARC gfx.lvl cx.lvl mx.lvl + level tables.
Step 2: QFPROM 0x221c8000 +0x118/+0x130 -> speed bin (FDT bins 0x00/0xc6 940 MHz, 0xbe 900,
0xb2 844, 0x8f 676, 0x82 612), gaming bin. Step 3 (only if gpu_cfg_ahb is on): GPU_CC
0x3d90000 PLL0/PLL1 (Lucid EVO), RCGs xo/gmu/hub/ff, 16 branches, CX GDSC 0x9108, GX GDSC
0x905c. Writes nothing; refuses gpu=1. Verified: 213 patches, the series reproduces the
three files exactly (bsd/conf/files from pristine xnu-8796.141.3 + all 16 patches touching
it); syntax-only compile clean (0 diagnostics). **Local syntax check that works now** (no
swiftlang clang needed): Xcode 27 `xcrun clang -fsyntax-only -arch arm64 -mkernel -nostdinc
-std=gnu11 -Werror=implicit-function-declaration -DKERNEL -DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE
-DBSD_KERNEL_PRIVATE -DPRIVATE -DAPPLE -DNeXT -DMACH_BSD -DDEVELOPMENT=1
-DARM64_BOARD_CONFIG_SWEET -D__arm64__`, run in temp/ios-m8/buildtree/build/xnu.obj/
DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT with -I. -I<bt>/xnu/bsd and -I<bt>/build/xnu.obj/
EXPORT_HDRS/{osfmk,libkern,pexpert,iokit} -I<bt>/xnu/EXTERNAL_HEADERS (buildtree = Oct 19,
pre-merge headers). It flags the 0967-style undeclared call (negative control checked).
**Test:** CI -> boot image with the standard cmdline + `sweetusb=2 kgpu=3`; start udp-log
AFTER the USB link is up (replay), watch for "SWEET KGPU" ~60 s after boot. Possible
outcomes: all three steps print (read access OK under Gunyah); a silent reset right after an
"about to read" line (that block is hyp-protected: read the tail with
shim-boot-logview2.img); step 3 refusing because gpu_cfg_ahb is off (then step 3 needs one GCC
write first).

**BOOT #27 (2026-10-23 16:44, shim/out/shim-boot-27.img = #26 + `kgpu=3`, kernel CI 39946814897
@ 8637d28 sha256 1b857603...): PROBE RAN, ALL THREE STEPS, NO RESET.** Log:
temp/ios-m8/usblog/live-boot27-20261023-1645.txt (listener started after the agent answered:
full replay). System as #26 (no panic, only the usual driverkitd kill). Results:
- 1 GCC: gpu_cfg_ahb 0x9 ON; memnoc_gfx 0x80000003 and snoc_dvm_gfx 0x80000001 enabled but
  CLK_OFF; GPLL0 votes (0x62000 bits 15/16) off (vote word 0x60257f). cmd-db OK (v1): gfx.lvl
  addr 0x30050, levels 0 56 64 128 192 224 256 320 384 416; cx.lvl 0x30000, 0 16 64 128 192 224
  256 288 320 336 384 400 416 432; mx.lvl 0x30010, 0 16 56 64 128 192 224 256 288 320 336 384
  400 416 432 (aux = 16 slots, zero-padded).
- 2 QFPROM +0x118 = 0x18c000 -> **speed bin 0xc6** (FDT: 9 levels, 295-940 MHz); gaming bin 1.
- 3 GPU_CC: **the GPU is entirely off, as ABL left it**: PLL0/PLL1 mode 0x100, L 0 (unconfigured,
  not locked); RCG xo/hub root on, gmu/ff root off (cfg 0); branches on: ahb, crc_ahb, cxo_aon,
  demet, hub_aon; everything else off; CX GDSC 0x22f001 and GX GDSC 0x22f001 = off, SW_COLLAPSE;
  gx_domain 0x3c0001, cx_hw_ctrl 0x5ff4, gx_sw_reset 0.
Meaning: GCC, cmd-db, QFPROM and GPU_CC are readable from our kernel under Gunyah (open
question 1, the read half, for these blocks). NEXT = steps 4-5, the first writes: CX GDSC on
(SW_COLLAPSE off, poll PWR_ON) and the CX-side clocks (GMU RCG/PLL, cx_gmu, cxo, hub_cx_int,
hlos1_vote_gpu_smmu, GCC memnoc/snoc_dvm), each behind kgpu=N with the same announce/pause.

**GPU STEPS 4-5 = 0972 (2026-10-23, written, NOT built/booted): the first GPU writes, `kgpu=5`.**
Step 4: CX GDSC (0x3d99108) SW_COLLAPSE off, PWR_ON awaited in GDSCR or hw-ctrl 0x3d9953c
(<=50 ms); refuses if CX is under HW control or GX already has power; CX already on = left
alone. Step 5 (kgsl gen7_gmu_enable_clks order; FDT GMU clocks decoded: gmu_clk = gpucc
cx_gmu, cxo_clk = cxo, axi_clk = GCC ddrss_gpu_axi 0x81154, memnoc_clk = GCC gpu_memnoc_gfx,
ahb_clk = gpucc ahb, hub_clk = hub_cx_int, smmu_vote = hlos1_vote_gpu_smmu, apb_pclk = aoss):
GCC 0x62000 bit 16 (GPLL0 div tap vote), ddrss_gpu_axi on; gmu_clk_src cfg 0x602 (200 MHz),
hub_clk_src cfg 0x603 (150 MHz), both GPLL0_OUT_MAIN_DIV (source 6, as hk on redwood: no
GPU PLL needed); cxo, cx_gmu, hub_cx_int must show running (BRANCH_HALT), smmu vote reported
only. Every write prints "SWEET KGPU: N write <block> <name> +<off>: old -> new" BEFORE it
happens and goes on an undo list; after kgpu_hold s (default 5) a state line, then everything
is undone in reverse (branches, RCGs, GCC, CX collapse last; "SWEET KGPU: undo ..."). Verified:
214 patches, the series reproduces both files, syntax check 0 diagnostics. **Test:** CI ->
#27 image with `kgpu=5` instead of `kgpu=3`. Outcomes: a reset right after a "write" line =
Gunyah forbids that write (persistent log); "STAYS OFF" / "does not take its new source" = a
clock prerequisite is missing (the line says which); all running + undo lines = CX power and
clocks work, next are steps 6-7 (first GMU register reads, RSCC).

**BOOT #28 (2026-10-23 17:08, shim/out/shim-boot-28.img = #27 with `kgpu=5`, kernel CI 39949651460
@ d5e49a2 sha256 72878422...): STEPS 4-5 WORK, ALL UNDONE, NO RESET.** Log:
temp/ios-m8/usblog/live-boot28-20261023-1708.txt. Steps 1-3 identical to #27. Step 4: write
cx_gdscr 0x22f001 -> 0x22f000 => **CX ON** at once (gdscr 0xf822f000, hw_ctrl 0x80001fe0).
Step 5: GCC vote 0x60257f -> 0x61257f; ddrss_gpu_axi 0x300001 running (already enabled);
GCC memnoc_gfx 0x3 and snoc_dvm_gfx 0x1 now RUNNING (they were enabled-but-off: they only needed
CX); gmu_clk_src cfg 0 -> 0x602 and hub_clk_src 0 -> 0x603, UPDATE cleared, both root on;
cxo 0x1, cx_gmu 0x221, hub_cx_int 0x1 running WITHOUT a write (ABL leaves their enable bits
set; "off" in step 3 = no CX power); hlos1_vote_gpu_smmu 0x80000000 -> 0x80000001 running. 5
writes total. Held 5 s unchanged, then undone in reverse; after: cx gdsc 0x22f001, hw_ctrl
0x5ff4, vote 0x60257f, RCG cfgs 0, smmu vote 0x80000000 (= the step-3 state). System fine
(agent up, no panic; crashes: driverkitd as always, one PosterBoard sig 5 as in #24b).
**Gunyah lets HLOS write GPU_CC and the GCC vote** (open question 1, write half, for these).
NEXT: steps 6-7 (read-only again, with CX + clocks on): GMU registers at 0x3d68000 (an AO
counter that should count, CM3 state), the bus interface, the GMU's RSCC 0x3d50000 / PDC.

**GPU STEPS 6-7 = 0973 (2026-10-23, written, NOT built/booted), `kgpu=7`, read-only** (steps
4-5 still power and clock CX first and are undone after the hold). Registers by kgsl gen7_reg.h
number (dwords from 0x3d00000; GMU node 0x3d68000+0x37000 also covers GPU_CC and CX misc).
Step 6, five announced groups: CM3 sysreset/boot_config/fw_init_result/cfg; HFI ctrl status,
general_7, rpmh_power_state, gfx_pwr_clk_status, keepalive, AO host irq / cx busy / ahb fence;
CX_AO_COUNTER (0x1f880, what kgsl reads) and ALWAYS_ON_COUNTER (0x1f888), each with its rate
over 10 ms ("THE GPU BLOCK ANSWERS" if one runs at 19.2 MHz); first 4 words of ITCM 0x1b400
and DTCM 0x1c400; GBIF 0x3d0f000 cx_config/scache_cntl0/halt/halt_ack; CX misc AHB controls
0x27810-14. (The gen7 CX timer kgsl seeds is gen7_9_x only: not ours.) Step 7: RSCC 0x3d50000
status0, pdc_slave_id, seq_busy, seq_mem_0, drv config, slot irq enable/pending, 4 slots
(wait/control/idle/enabled + first command), GMU rscc_control_ack 0x23b08; PDC 0xb270000
enable 0x4500 / seq_start 0x4520. Verified: 215 patches, series reproduces the file, syntax
check clean. **Test:** CI -> #28 image with `kgpu=7`. A reset after an "about to read" line =
that block is not reachable (or needs something kgsl does that we skip); counters at 19.2 MHz
= the GMU side answers; then steps 8-9 (SMMU ID registers, first context, read-only).

**BOOT #29 (2026-10-23 17:31, shim/out/shim-boot-29.img = #28 with `kgpu=7`, kernel CI 39952140324
@ 4fcd0ce sha256 5bb229d3...): STEPS 6-7 READ CLEAN, UNDO CLEAN, NO RESET.** Log:
temp/ios-m8/usblog/live-boot29-20261023-1731.txt. System as before (driverkitd only, no panic).
- 6 GMU: cm3 sysreset 0x1 (the GMU's CM3 is held in reset), boot_config 0, fw_init_result 0
  (no firmware), cm3_cfg 0x4052; hfi_ctrl_status 0, general_7 0, rpmh_power_state 0xf,
  gfx_pwr_clk_status 0x3, keepalive 0; AO host irq 0, cx_busy 0x30000000, ahb_fence 0.
  **ALWAYS_ON_COUNTER (0x1f888) runs at 19,198,540 Hz: THE GPU BLOCK ANSWERS.** CX_AO_COUNTER
  (0x1f880, the one kgsl reads) = 0, not counting: started by something later (GMU firmware?).
  ITCM/DTCM first words = random SRAM content (readable). GBIF cx_config 0x20001, scache_cntl0 0,
  **halt 0x8, halt_ack 0x3** (kgsl writes GBIF_HALT=0 before GMU boot, adreno_gen7_gmu.c:1007,
  "may not get cleared if the gdsc was not reset": part of step 15). CX misc AHB controls all 0.
- 7 RSCC 0x3d50000: status0 0, pdc_slave_id 0x1, seq_busy 0, **seq_mem_0 0 (no RSC microcode:
  kgsl loads 0xeaaae5a0 there, step 16)**, drv_config 0x4000000a, control_ack 0; slot irq 0/0;
  slots 0-3 idle 0x1, nothing enabled. PDC 0xb270000: enable 0x101, seq_start 0 (kgsl writes
  both in gen7_load_pdc_ucode, step 16; a6xx's enable value is 0x80000001, gen7's not in our tree).
Meaning: GMU, GBIF, CX misc, RSCC and PDC are all reachable with CX on. NEXT: steps 8-9, the GPU
SMMU (qsmmu-v500 0x3da0000): ID registers, stream matching, first context (read-only), then the
TrustZone questions (11-12).

**GPU STEPS 8-9 = 0974 (2026-10-23, written, NOT built/booted), `kgpu=9`, read-only SMMU.** FDT
kgsl_smmu: qcom,qsmmu-v500 + adreno-smmu at 0x3da0000 (+0x40000), tcu 0x3de6000, TBUs gfx_0
0x3de9000 (streams 0x0-0x3ff) / gfx_1 0x3ded000 (0x400-), qcom,skip-init, 3-level tables,
dma-coherent, **qcom,num-context-banks-override = 6, qcom,num-smr-override = 6** (HLOS gets 6 of
each; 0974 reads only those), vdd = gpu_cc_cx_gdsc. Streams (iommus, mask 0x400): gfx3d_user 0x0,
gfx3d_secure 0x2, GMU 0x5 (0x7/0x407 = iommu-debug test cases). Step 8: IDR0/IDR1 layout (page
size, pages, SMRs, contexts; stop unless 2*pages*page == 0x40000), IDR2/IDR7/sCR0/sGFSR, SMR/S2CR
0-5 decoded, CBAR/CBA2R 0-5, and which entry takes streams 0/2/5. Step 9: CB0 SCTLR/ACTLR/TTBR0/
TCR/MAIR0/FSR/FAR (+ the CB stream 0 maps to, if another of the 6). Verified: 216 patches, series
reproduces the file, syntax clean. **Test:** CI -> #29 image with `kgpu=9`. A reset after a
step 8 "about to read" = Gunyah traps the SMMU's global page (hk: on redwood the hypervisor
"answers for" it); after step 9's = the context page. Then step 10 (two words into GMU DTCM and
back: the first GMU write).

**BOOT #30 (2026-10-23 17:53, shim/out/shim-boot-30.img = #29 with `kgpu=9`, kernel CI 39955079762
@ da7eb39 sha256 8a20353d...): STEPS 8-9 READ THE SMMU CLEAN, UNDO CLEAN, NO RESET.** Log:
temp/ios-m8/usblog/live-boot30-20261023-1753.txt. System as before (driverkitd only).
- 8 IDR0 0x4c017e18 / IDR1 0x40000016: **24 SMRs, 22 contexts, 32 pages of 4K** = 0x40000 as the
  FDT says (22 matches the dossier). IDR2 0x5511, IDR7 0x24, **sCR0 0x250406** (CLIENTPD clear =
  SMMU on; USFCFG set = unmatched streams FAULT; GFRE/GFIE on), sGFSR 0. **The 6 HLOS entries are
  empty**: SMR0-5 = 0 (unused), S2CR0-5 = 0x20000 (fault), CBAR/CBA2R0-5 = 0. So streams 0 (GPU),
  2 (secure), 5 (GMU) have no route and would fault today: setting SMR/S2CR/CB is ours (step 13,
  like kgsl's arm-smmu attach; skip-init keeps whatever the boot chain set, and it set nothing).
- 9 CB0: SCTLR 0x70e (M=0, translation off), ACTLR 0x3, TTBR0 0x2e01032f:40804030, TCR 0x588253aa,
  MAIR0 0xf020241, FSR 0, FAR 0x350:82232b1d: leftovers (CB registers are not reset), not a
  configuration.
Meaning (open question 1, read half): **every GPU block is readable from our kernel under Gunyah**
(GCC, cmd-db, QFPROM, GPU_CC, GMU, GBIF, CX misc, RSCC, PDC, SMMU global + CB0). Whether the
SMMU's values are the hardware's or the hypervisor's own, they are consistent with an MMU-500.
NEXT: step 10, the first write to the GMU (two words into DTCM and back, restored), then the
TrustZone questions (11: SCM availability, 12: zap .mdt signature check).

**GPU STEP 10 = 0975 (2026-10-23, written, NOT built/booted), `kgpu=10`: the first GMU write.**
Refuses unless CM3_SYSRESET bit 0 is set (processor in reset; #29: 0x1) and FW_INIT_RESULT bit 8
is clear (no firmware started; #29: 0). Then two logged writes, 0x600d0001 / 0x0a110002, into
DTCM 0x1c400/0x1c401 (0x3d71000), read back, and the old words restored at once through the
undo list (new kg_undo_pop(), also used by the teardown; the restores print "undo dtcm ...").
Success line: "the GMU's memory TAKES WHAT IS WRITTEN: its firmware can be loaded". Verified:
217 patches, series reproduces the file, syntax clean. **Test:** #30 image with `kgpu=10`.

**BOOT #31 (2026-10-23 18:14, shim/out/shim-boot-31.img = #30 with `kgpu=10`, kernel CI 39957643812
@ b7db169 sha256 7e7927b2...): STEP 10 PASSES, NO RESET.** Log:
temp/ios-m8/usblog/live-boot31-20261023-1814.txt. CM3 sysreset 0x1, fw_init_result 0; DTCM
+0x9000 0xdc117fdb -> 0x600d0001, +0x9004 0x0736439a -> 0x0a110002; read back 0x600d0001
0x0a110002 = **the GMU's memory takes what is written (firmware can be loaded this way)**; old
words restored at once (undo lines), then the usual 5-write undo; CX back to 0x22f001. System as
before (driverkitd only). (DTCM's leftover words differ per boot: #29 read 0x8c117fdb first;
power-up SRAM contents.) First write to the GMU block itself: allowed under Gunyah.
NEXT: steps 11-12, TrustZone: 11 = SCM calls that pass no memory (does TZ answer this kernel;
is PAS 13 = GPU zap supported), 12 = zap .mdt signature check (PAS init image with the
a710_zap.mdt header in memory TZ is pointed at). Both are SCM calls, not MMIO: our SCM/PAS path
exists only in TopazModem (0113+, never run on kunzite).

**GPU STEPS 11-12 = 0976 (2026-10-23, written, NOT built/booted), `kgpu=12`: TrustZone.** Calls go
through TopazModem's exported topaz_scm (SiP, SMC64, yielding = Linux qcom_scm-smc; resumes
"interrupted"); fw via topaz_fw_read (root FS). All four symbols confirmed exported in the CI
kernel. Step 11 (no memory passed): is_call_available(is_call_available) must say 1 (else
"TrustZone does not answer the way it answers Linux" and stop); is_call_available for PAS init
image / mem setup / auth+reset / shutdown / is_supported; pas_is_supported for 1 ADSP, 4 modem,
9 video, 13 GPU. Step 12: reads /usr/share/firmware/kunzite/a710_zap.mdt (4044 bytes: ELF32
header 52 + 3 phdrs 96 + hash segment 3896 = the metadata TZ wants as is; checked), writes it
with aligned words into the uncached page 0xdff00000 (low DDR: Android "System RAM", never
XNU's; below 4 GiB like Linux's buffer, qcom_scm's default 32-bit DMA mask), then
PAS_INIT_IMAGE(13, 0xdff00000), arginfo VAL,RW. Success line: "TRUSTZONE ACCEPTS THE ZAP
SHADER'S SIGNATURE FROM THIS KERNEL". Nothing is loaded or started; the page is not touched again
(TZ may XPU-protect it); TZ keeps the pending PAS 13 state until reboot (as hk). Verified: 218
patches, series reproduces the file, syntax clean. **Before the test, once:** `curl -s -T
temp/kunzite-vendor/proprietary/vendor/firmware/a710_zap.mdt
'http://192.168.7.2:8022/put?p=/usr/share/firmware/kunzite/a710_zap.mdt&m=644&mk=1'` (vendor
firmware: on the phone only, never in git). **Done 2026-10-23 during boot #31**: 4044 bytes, 0644
root, read back with /get = identical (sha256 5e2e0c88...); it stays on the root FS. **Test:** #31 image with `kgpu=12`. A reset right
after an "smc" line = Gunyah/TZ does not take that call from us.

**BOOT #32 (2026-10-23 18:33, shim/out/shim-boot-32.img = #31 with `kgpu=12`, kernel CI 39960767848
@ 39c4977 sha256 158e7a70...): TRUSTZONE ACCEPTS THE ZAP SHADER'S SIGNATURE FROM THIS KERNEL.**
Log: temp/ios-m8/usblog/live-boot32-20261023-1833.txt. Steps 1-10 as before (step 10 passed again).
- 11: smc 0x42000601 is_call_available(0x2000601) -> ret 0 res 1: **TZ answers this kernel the
  way it answers Linux** (through Gunyah, SiP SMC64 yielding). PAS init image / mem setup / auth
  and reset / shutdown / is_supported: all AVAILABLE (res 1). pas_is_supported(1, 4, 9, 13) ->
  ret 0 res 0 for ALL, even ADSP and modem which the boot chain starts: on this firmware that
  question tells nothing (as hk's fallback case), not "13 is unsupported".
- 12: /usr/share/firmware/kunzite/a710_zap.mdt read (3 phdrs, hash 3896, 4044 bytes), written
  to 0xdff00000 (no fault: that low-DDR page is ours to write), **smc 0x42000201
  PAS_INIT_IMAGE(13, 0xdff00000) -> ret 0 res 0: the signature check passes.** TZ now holds a
  pending PAS 13 image until reboot (nothing loaded/started).
Then the usual 5-write undo; system as before (driverkitd only, no panic).
Cosmetic for the next patch: step 11's groups say "about to read nothing; next: ..." (kg_announce
used where kg_next was meant).
**Open question 2 ANSWERED: TZ takes PAS calls for the GPU from our kernel.** NEXT: step 13, the
first SMMU writes: GMU memory through the SMMU (stream 5 -> an HLOS SMR/S2CR/context, our page
tables, 3-level 4K, 39-bit, as hk/io-pgtable-arm), then 14 (GMU firmware gmu_gen70000.bin into
ITCM/DTCM + its blocks), 15 (GMU out of reset, GBIF halt cleared first).

**GPU STEP 13 = 0977 (2026-10-23, written, NOT built/booted), `kgpu=13`: the first SMMU writes.**
Stream 5 (GMU) -> SMR/S2CR entry 1 -> context 1 (0 stays the GPU's, as Linux's adreno-smmu),
refused unless entry 1, CB1 and stream 5 are unused (#30: all empty). One kernel page (kmem_alloc,
kvtophys; both exported) holds L1/L2/L3 tables (+0/+0x1000/+0x2000; 3 levels, 4K, 39-bit) and a
test page (+0x3000, filled 0x6a000000|i) mapped at GMU VA 0x60000000 (PTE outer-shareable, AF,
nG, attr 0 = uncached), cache-cleaned (dc civac). Context programmed as hk's redwood MMU-500 took
it: SCTLR 0, CBA2R VA64, CBAR stage-1 (0x1f300), TCR2 0x38011 (36-bit PA, 16-bit ASID), TCR
0x803519 (T0SZ 25, WBWA, ISH, EPD1), TTBR0 = tables | ASID 1 << 48, TTBR1 0, MAIR0 0xf404ff44,
MAIR1 0; then (logged commands, not undone) FSR W1C, TLBIASID 1, TLBSYNC, poll TLBSTATUS; then
SCTLR on (M TRE AFE CFRE ASIDPNE), S2CR1 = 1, SMR1 = valid | 0x400 << 16 | 5; all read back.
**IDR0 has ATOSNS: no address-translation operation**, so step 13 proves the configuration is
taken and kept (readbacks, FSR/sGFSR 0, TLB sync finishes), not a translation; the GMU's own
reads (steps 14-15) are that proof. Undo list now 40 entries, new kind KFREE: the end of the run
unroutes stream 5, restores CB1/CBAR1/CBA2R1, frees the page last. Also step 11's groups say
"next:". Verified: 219 patches, series reproduces the file, syntax clean. **Test:** #32 image
with `kgpu=13`. A reset right after a "write SMMU" line = Gunyah forbids that SMMU write.

**BOOT #33 (2026-10-23 18:53, shim/out/shim-boot-33.img = #32 with `kgpu=13`, kernel CI 39963657778
@ b0fcc04 sha256 94fc689f...): STEP 13 WORKS, THEN A KERNEL PANIC DURING THE UNDO.** Log:
temp/ios-m8/usblog/live-boot33-20261023-1853.txt (ends at the panic). Steps 1-12 as #32 (zap
signature accepted again). Step 13: entry 1 / CB1 / stream 5 free (CB1 leftovers: sctlr 0x1a2112,
tcr2 0x20060, tcr 0x41f0203, ttbr0 0x233bc8a:08009662, mair0 0x24042 ...); tables at 0xa2c7c8000
(above 4 GiB, 36-bit OK); all 11 context writes + FSR/TLBIASID/TLBSYNC (tlbstatus 0: sync done) +
SCTLR/S2CR/SMR taken; read back smr1 0x84000005, s2cr1 0x1, cbar1 0x1f300, cba2r1 0x1, sctlr
0x27 (ASIDPNE bit 12 not kept), tcr 0x803519, tcr2 0x38071 (bits 5-6 read as 1), ttbr0
0x1000a:2c7c8000, mair0 0xf404ff44, fsr 0x400 (format bit only), sgfsr 0 => **"THE SMMU TAKES OUR
TABLES": Gunyah lets HLOS program the GPU SMMU (open question 1, write half: SMMU yes).** Then
"held 5 s" (CX/clocks as always), "undoing 20 writes, in reverse", the 1.5 s pause, and NO "undo"
line ever arrived: **kernel panic** (user: on screen, ~1 min at the lock screen). A panic stops the
USB log, so the culprit is not in it. Suspects: (1) kmem_free of the KMA_KOBJECT|KMA_DATA table
page (new path: hk never freed its tables), (2) one of the 13 SMMU restores writing CB1's junk
leftovers back. The panic screen's text (keepsyms=1 => names) decides.
**#33 PANIC DECODED (user's screen, symbolized against the booted kernel 94fc689f...):** "Kernel
data abort" at pc 0xfffffe0007521a00 = **kg_undo_pop+0x74, `str w20, [x23]`** (the KG_UNDO_WORD
restore), lr kg_thread+0x280 (kg_teardown inlined); esr 0x96000010 (synchronous external abort;
WnR not trustworthy here, the instruction is a store); far 0xfffffe10003c1004 = SMMU base
(x19 0xfffffe10003c0000) + **0x1004 = CBAR1**, value w20 = 0. The undo had already restored SMR1,
S2CR1, SCTLR, MAIR0/1, TTBR0/1, TCR, TCR2. **Writing CBAR1 := 0 (type 0 = a STAGE-2 context) on
this stage-1-only SMMU (IDR0 S1TS, no S2TS) is rejected with an external abort** (SMMU or
Gunyah); 0 was only the power-up value. Our 0x1f300 (stage 1, S2 bypass) was accepted. Linux's
arm-smmu releases a context with SCTLR = 0 only (arm_smmu_write_context_bank, !cfg). Fix: undo
only SMR (0), S2CR (fault), SCTLR (0, not the junk leftover); log but never restore CBAR/CBA2R/
TCR/TCR2/TTBR/MAIR; allocate the tables with KMA_DATA|KMA_ZERO (the pair topaz_sandbox_container.c
already allocates and kmem_frees on every op 4) instead of hk's KMA_KOBJECT|KMA_DATA, whose
kmem_free has never run here.
**FIX = 0978 (2026-10-23, written, NOT built/booted):** step 13's undo now releases CB1 the Linux
way: undo entries only for SMR1 (back to 0), S2CR1 (back to fault 0x20000) and SCTLR (0, pushed
before enabling, so it pops after S2CR/SMR); CBA2R/CBAR/TCR2/TCR/TTBR0/1/MAIR0/1 are written with
new kg_set() ("... (kept)" in the log) and never restored. Tables: kmem_alloc(KMA_DATA|KMA_ZERO,
VM_KERN_MEMORY_BSD). The "in use" guard no longer reads CBAR1: CX's GDSC has qcom,retain-regs, so
CB1's CBAR = 0x1f300 (#33's rejected store never cleared it) may still be there next boot; free =
SMR1 invalid + CB1 SCTLR.M clear + stream 5 unmatched. Expected #34 log: "N writes to undo" = 9
(5 of steps 4-5 + KFREE, SCTLR, S2CR, SMR), undo lines smr1, s2cr1, cb1 sctlr (off), "freed 0x4000
bytes", then the usual 5, "run over". Verified: 220 patches, series reproduces the file, syntax
clean, no CBAR/CBA2R/CB undo left. **Test:** reboot the phone (it is on the #33 panic screen) to
fastboot, CI -> #33 image again with this kernel, `kgpu=13`.

**BOOT #34 (2026-10-23 19:19, shim/out/shim-boot-34.img = #33 with the 0978 kernel, CI 39966958819
@ 31ab72f sha256 45f89519...): STEPS 1-13 AND THE UNDO RUN CLEAN, NO PANIC (0978 works).** Log:
temp/ios-m8/usblog/live-boot34-20261023-1918.txt. Step 13 as #33 (context writes "(kept)", TLB sync
done, identical readbacks, "THE SMMU TAKES OUR TABLES"); tables at 0xa290d8000. "9 writes to
undo": smr1 -> 0, s2cr1 -> 0x20000, cb1 sctlr (off) -> 0, "gmu tables and test page: freed 0x4000
bytes" (kmem_free with KMA_DATA|KMA_ZERO works), then the 5 of steps 4-5, CX back to 0x22f001,
"run over". System as before (driverkitd only). CBAR1 read 0 at the start (not #33's 0x1f300) and
CB1's leftovers differ again (sctlr 0x4a2112): the SMMU state did NOT survive the reboot despite
retain-regs; the CBAR-blind guard is still right within a boot. **Steps 1-13 of the port map are
proven on kunzite.** NEXT: step 14, the GMU firmware (gmu_gen70000.bin: kgsl gen7_gmu_parse_fw /
load_fw / itcm_shadow, its blocks into ITCM/DTCM and the VMAs the blob asks for through context
1), then 15 (GBIF_HALT = 0, CM3_SYSRESET = 0, wait FW_INIT_RESULT): the first proof that the GMU
reads through our SMMU tables.

**GPU STEP 14 = 0979 (2026-10-23, written, NOT built/booted), `kgpu=14`: the GMU firmware.**
gmu_gen70000.bin (57272 bytes, 15 blocks): 11 size-0 requests (ITCM 0x0/0x200, DTCM 0x10004000/
0x10007000/0x10007fdc: nothing to do; ICACHE 0x4000+0xdcac, 0x100000+0x88b0, 0x200000+0x12a80,
0x300000+0x6e28; PERSIST 0x60000000+0x1000, 0x60001000+0x1000) and 4 data blocks of type
0xefbeadde (ITCM 0x0+0x1dc = vector table, SP 0x10007f80; code ICACHE 0x4000+0xdcac; DTCM
0x10007f98+0x28 and 0x10007fdc+0x18, the latter holding the versions). kgsl gen7 GMU VAs: ITCM 0
(16K), ICACHE 16K..16M, DTCM 0x10004000 (16K), uncached kernel 0x60000000 (512M), extended
0xc0000000. 0979: step 13's tables now come from a pool of 8 (two 16K pages, made before the
context is on, freed after stream 5 is unrouted), test page moved to GMU 0x7ffff000; step 14 =
parse (6 blocks allocated KMA_DATA|KMA_ZERO, mapped page by page through context 1, PTE_GMU),
tables committed, TLBIASID+sync, load (ITCM/DTCM via registers, each word read back; the code
memcpy'd + cache-cleaned), versions at DTCM+0xff8 (expected core 0x40000007 >= 0x40000000, hfi
0x20010000). Python simulation against the real file: 6 allocs, 7 tables, all loads resolve.
Undo peaks at 17 entries. CM3 stays in reset; TCMs not restored. Verified: 221 patches, series
reproduces the file, syntax clean. **Before the test, once:** `curl -s -T temp/kunzite-vendor/
proprietary/vendor/firmware/gmu_gen70000.bin 'http://192.168.7.2:8022/put?p=/usr/share/firmware/
kunzite/gmu_gen70000.bin&m=644&mk=1'` (vendor firmware, phone only). **Done 2026-10-23 during boot #34**: 57272
bytes, 0644 root, read back with /get = identical (sha256 b2d3f0e2...). **Test:** #34 image with
`kgpu=14`. Success line: "THE GMU FIRMWARE IS LOADED (the CM3 stays in reset)".

**BOOT #35 (2026-10-23 20:45, shim/out/shim-boot-35.img = #34 with `kgpu=14`, kernel CI 39970119367
@ 7f9dab0 sha256 8e25dc4a...): STEP 14 PASSES, "THE GMU FIRMWARE IS LOADED", UNDO CLEAN, NO PANIC.**
Log: temp/ios-m8/usblog/live-boot35-20261023-2045.txt. Step 13 with the pool: "8 tables in 2 pages,
root 0xa2444c000", test page at GMU 0x7ffff000, "THE SMMU TAKES OUR TABLES" again. Step 14: CM3
in reset (sysreset 0x1, fw_init_result 0); 6 blocks mapped through context 1 (0x4000 14 pages,
0x100000 9, 0x200000 19, 0x300000 7, 0x60000000 1, 0x60001000 1); TLBIASID+sync; ITCM 119 words via
register 0x1b400, **0 of 119 differ on readback**; code 0xdcac bytes copied to GMU 0x4000 (first
word 0xbd1fb51f); DTCM 10 words via 0x1d3e6 and 6 via 0x1d3f7, **0 differ**; versions core
0x40000007, core_dev 0xffffffff, pwr 0x40000007, pwr_dev 0, hfi 0x20010000 (exactly the Python
prediction). 17 undo entries all ran: six "gmu firmware memory" frees, smr1 0, s2cr1 0x20000, cb1
sctlr 0, test page and both table pages freed, then the 5 of steps 4-5; "run over". System as
before (driverkitd only). **Steps 1-14 proven.** NEXT: step 15, start the GMU (kgsl
gen7_gmu_first_boot order after load_fw: version_info, itcm_shadow, [scm cx regs], register_config,
irq enable, bus vote, device_start = CM3_SYSRESET 0 + poll FW_INIT_RESULT bit 8; GBIF_HALT 0
first): needs a read of gen7_gmu_register_config / gen7_gmu_device_start's preconditions.

**GPU STEP 15 = 0980 (2026-10-23, written, NOT built/booted), `kgpu=15`: the GMU starts.** Read
from kgsl (temp/gpu-plan-review/kgsl): gen7_gmu_first_boot after load_fw = version_info,
itcm_shadow (a copy for snapshots), scm_gpu_init_cx_regs (BCL/CLX/tsense via TZ; EOPNOTSUPP is
ignored, skipped here), register_config, irq_enable, bus vote, device_start. 0980 does
register_config in kgsl's order, all kept: FALNEXT/FAL 1, FW_INIT_RESULT 0, BOOT_CONFIG 2,
HFI_QTBL_ADDR 0x60012000 + INFO 1, AHB_FENCE_RANGE_0 0x80c808a0, CM3_CFG 0x4052, gen7_3_0 GBIF QSB
SIDE0-3 0x00071620, GBIF_HALT 0, GENERAL_9 0 (log wptr), GENERAL_10 0x07010000 (chipid),
GENERAL_8 0x6000200f (log 0x60002000, 16 pages), SYS_BUS/ICACHE/DCACHE_CONFIG 1; then
irq_enable's two clears (masks left alone: we poll). Before that: a 64K log at 0x60002000 and 16K
HFI at 0x60012000 (kgsl's next VAs after the firmware's 0x60000000/0x60001000), the queue table
as gen7_hfi_init writes it (840 bytes = 6 + 17x12 words; queues at +0x350/+0x1350/+0x2350, types
0xa00/0xa01/0x2802, 1024 words), every GMU block flushed (KMA_ZERO zeroes in the cache would
land on what the GMU writes), TLBIASID+sync. Then CM3_SYSRESET 0 and FW_INIT_RESULT polled for
0x100 under mask 0x1ff (kgsl gives 100 ms; here 500), bits 11:9 = stopped in the NMI handler.
Printed after: fw_init_result, HFI_CTRL_STATUS, GMU2HOST_INTR_INFO, AO_HOST_INTERRUPT_STATUS,
sysreset; CB1 FSR/FAR/FSYNR0 and sGFSR; non-zero words in the log's first page and its first 4;
the three queues' read/write indices. Undo (a new KG_UNDO_CM3 entry pushed just before the
release, so it runs first): gen7_gmu_pwrctrl_suspend's order = AHB_FENCE_STATUS_CLR 7,
AO_AHB_FENCE_CTRL 0, CM3_SYSRESET 1, GBIF_HALT ARB (bit 1) + wait HALT_ACK, then the frees (now 8
"gmu firmware memory"), stream 5 unrouted, CX down. Undo peaks at 20 entries (max 40). Verified:
222 patches, series reproduces the file, syntax clean. **Test:** #35 image with `kgpu=15`.
Success line: "THE GMU'S FIRMWARE RUNS"; worth reading even on failure: CB1 FSR/FAR (the GMU
fetching an address we did not map), the log count (the GMU writing through our tables).

**BOOT #36 (2026-10-23 21:11, shim/out/shim-boot-36.img = #35 with `kgpu=15`, kernel CI 39983029309
@ b1e32e0 sha256 9d29069b...): STEP 15 PASSES, "THE GMU'S FIRMWARE RUNS", UNDO CLEAN, NO PANIC.**
Log: temp/ios-m8/usblog/live-boot36-20261023-2112.txt. Steps 1-14 as #35 (same 6 blocks, 0 words
differ, versions 4.0.7). Step 15: log 0x60002000 (16 pages) and HFI 0x60012000 (4 pages) mapped,
queues at 0x60012350/0x60013350/0x60014350, TLB sync. Values before the writes: FAL/FALNEXT 0,
CM3_CFG already 0x4052, QSB sides 0x70620 (-> 0x71620), GBIF halt 0x8 (-> 0; halt_ack stays 0x3,
as in step 6), the rest 0. Then CM3_SYSRESET 1 -> 0 and **FW_INIT_RESULT 0x100** (kgsl's success
value); HFI_CTRL_STATUS 0x1 (0 in step 6: the firmware set it), GMU2HOST_INTR_INFO 0,
AO_HOST_INTERRUPT_STATUS 0. CB1 FSR 0x400 = only the FORMAT field (AArch64), same as step 13's
readback: **no SMMU fault** (FAR/FSYNR0 are stale, not latched). **The GMU wrote its log through
our tables**: 15 non-zero words in the first page, starting 0x21 0x31dd9759 0x1 0. HFI queues
r/w all 0 (HFI not started, expected). So the CM3 fetched its vectors from ITCM, ran the code at
GMU 0x4000 (memory of ours, through context 1) and wrote to 0x60002000. 20 undo entries ran:
"undo gbif halt (cx traffic): ack in", cm3_sysreset -> 1, eight memory frees, smr1/s2cr1/cb1
sctlr, test page and tables freed, steps 4-5; CX back to 0x22f001; "run over". System as #35
(fairplayd/bluetoothd relaunches as before, agent up). **Steps 1-15 proven.** NEXT: step 16, GMU
<-> RPMh and HFI start: PDC (SEQ_START_ADDR 0, ENABLE_PDC) and RSC (handshake regs + microcode)
as kgsl gen7_load_pdc_ucode / gen7_load_rsc_ucode, then gen7_gmu_hfi_start (HFI_CTRL_INIT 1, poll
HFI_CTRL_STATUS bit 0 - already 1 here, so check what kgsl expects first).

**GPU STEP 16 = 0981 (2026-10-23, written, NOT built/booted), `kgpu=16`: RSC/PDC and HFI start.**
Read from kgsl: after device_start, first_boot loads PDC/RSC once (GMU_PRIV_PDC_RSC_LOADED) and
calls gen7_gmu_hfi_start = HFI_CTRL_INIT 1 + poll HFI_CTRL_STATUS bit 0 for 100 ms; nothing
clears the status first, so bit 0 already set (boot #36) satisfies it trivially - 0981 prints
before/after and looks for other signs (log growth, queue indices, GMU2HOST_INTR_INFO). RSC
(gen7_load_rsc_ucode, RSCC dwords): 0x4 = 1<<24, 0xb = 1, hidden TCS 0xe/0xd, 0x10/0xf = 0,
0x12 = 0x80000000 (0x80000021 only for gen7_2_x; gen7_3_0 is not), 0x11 = 0, 0x100 = 0, 0x8 =
0x4520, 0x9 = 0x4510, 0xa = 0x4514, SEQ_MEM_0 0x180..0x184 = eaaae5a0 e1a1ebab a2e0a581 ecac82e2
0020edad (gen7_2_x would use 0x154). PDC (gen7_load_pdc_ucode, gmu_pdc dwords): 0x1148 = 0, 0x1140
= 0x80000001; step 7 read 0x101 / 0 there (enabled by the boot). Boot-time RSCC words (step 7):
status0 0, slave_id 1, seq_mem_0 0. All 19 via kg_put (undone: the boot's words back; they pop
before step 15's CM3 reset, with the GMU idle); HFI_CTRL_INIT kept (GMU register, CX collapse).
Success line: "HFI IS UP (HFI_CTRL_STATUS bit 0, as kgsl waits for)" - plus "; bit 0 was set
before HFI_CTRL_INIT too" if so. Fails on: no bit 0, an RSCC word not kept (STATUS0 may have
read-only bits: look at the line), NMI bits, CM3 fault, CB1 fault. Risk: the PDC (0xb270000, AOSS)
is the first write outside the GPU block and GCC; a stage-2 refusal would be an external abort
(the line before names it). Undo peaks at 39 (KG_UNDO_MAX now 64). Verified: 223 patches, series
reproduces the file, syntax clean. **Test:** #36 image with `kgpu=16`.

**BOOT #37 (2026-10-23 21:29, shim/out/shim-boot-37.img = #36 with `kgpu=16`, kernel CI 39986193233
@ fdd113d sha256 b6b32353...): STEP 16 RUNS KGSL'S SEQUENCE WITHOUT A FAULT, BUT ITS CHECK IS TOO
STRICT; NO PANIC, UNDO CLEAN.** Log: temp/ios-m8/usblog/live-boot37-20261023-2129.txt. Steps 1-15
as #36 (FW_INIT_RESULT 0x100, 15 log words, now counted over the whole 16384-word log). Step 16
before: GMU running (sysreset 0), HFI_CTRL_STATUS 1, rscc seq_busy 0, PDC enable 0x101 seq_start 0.
**RSCC: 14 of 17 words take kgsl's values** (status0 0 -> 1<<24, the 5 sequencer words 0 ->
eaaae5a0.., cmd2 data 0 -> 0x80000000; slave_id 1, PDC_SEQ_START_ADDR 0x4520, MATCH 0x4510/0x4514
were already kgsl's values at boot). **The 3 hidden TCS command ADDR words ignore the zeroes**:
they read 0x82204514 / 0x82204510 / 0x82204520 before and after (cmd0/1/2: the PDC registers
+0x4514 MATCH_HI, +0x4510 MATCH_LO, +0x4520 SEQ_START that the other RSCC words name) - hardwired
or locked, so kgsl's zero writes do nothing there either. That alone failed the step ("the RSCC
does not keep kgsl's words"). **PDC: Gunyah allows HLOS writes to the GPU PDC** (0xb270000, the
first write outside the GPU block and GCC): enable 0x101 -> 0x80000001 read back, seq_start 0.
HFI: HFI_CTRL_INIT 1, HFI_CTRL_STATUS 1 -> 1 (was set before, as #36 found), no interrupt
(GMU2HOST_INTR_INFO 0), log 15 -> 15 words, all queues r/w 0/0, CB1 FSR 0x400 (no fault), rscc
seq_busy 0, control_ack 0. So HFI_CTRL_INIT shows no sign of its own; the proof that HFI works is
step 17's first message and its reply in the message queue. Undo: 39 entries; pdc enable back to
0x101, rscc words back (the 3 ADDR words never changed), then step 15's CM3 reset + GBIF halt
ack, frees, stream 5 unrouted, CX down; "run over". System as #36 (agent up). NEXT: step 17
(gen7_hfi_start's messages), with step 16's check fixed: a word that still reads its old value
after the write is "write ignored", printed but not a failure.

**GPU STEP 17 = 0982 (2026-10-23, written, NOT built/booted), `kgpu=17`: HFI messages.** Read from
kgsl adreno_gen7_hfi.c / adreno_gen7_rpmh.c / adreno_rpmh.c: gen7_hfi_start = reset_hfi_queues,
perf table, bw table, ACD/BCL/CLX/IFPC enables (gen7_3_0 has IFPC|BCL|ACD; not sent here),
core fw start, then default votes (not done). HFI 2.1 (0x20010000): legacy H2F_MSG_PERF_TBL (4,
hfi_dcvstable_cmd 59 words) and generic_req (ack word 2 = result; v5 only after minor 4).
Message header = seq<<20 | size<<8 | type<<16 | id; queues 1024 words, writes padded to 4 with
0xfafafafa; doorbell HOST2GMU_INTR_SET (0x1f994) = 1; ack wait = GMU2HOST_INTR_INFO bit 0, 1 s,
NMI (FW_INIT_RESULT 11:9) aborts. Inputs: FDT pwrlevels-5 = bin 0xc6 (kHz/vlvl: 940000/416
875000/384 816000/320 734000/256 650000/224 600000/192 500000/128 345000/64 295000/56), no
cx-level; GMU 200/500 MHz at 64/128 (kgsl defaults); ddr_device_type 7 -> bus-table-ddr7 (KBps 0
781250 2136718 3000000 3972656 5285156 6074218 6671875 8332031), cnoc 0/100. Predicted perf
table from #37's ARC levels (gfx 10, cx 14, mx 15): gx0 0, gx1 0x380201/295000 ... gx9
0x1a00d09/940000 (vote = vlvl<<16 | mx idx<<8 | gfx idx); gmu 0 / 0x400302 / 0x800403. BW votes
need the BCMs' unit/width/vcd from cmd-db (printed). The HFI block is used through an uncached
alias (ml_io_map_unmappable, VM_WIMG_WCOMB, unmapped before the step returns; ml_io_map's own
mappings are permanent and ml_io_unmap would kmem_free them from the wrong map): our read/write
indices share cache lines with the GMU's. Undo gains a CM3 entry first (GMU stopped before the
RSC/PDC words go back). Success line: "THE GMU ANSWERS OVER HFI: tables taken, core firmware
started". Verified: 224 patches, series reproduces the file, syntax clean. **Test:** #37 image with
`kgpu=17`.

**BOOT #38 (2026-10-23 23:12, shim/out/shim-boot-38.img = #37 with `kgpu=17`, kernel CI 39989602096
@ 32bd9ec sha256 bf6e0252...): STEP 17 PASSES, "THE GMU ANSWERS OVER HFI: tables taken, core
firmware started"; NO PANIC, UNDO CLEAN.** Log: temp/ios-m8/usblog/live-boot38-20261023-2312.txt.
Step 16 with the relaxed check: "14 of 17 words take kgsl's values, 3 ignore the write, 0 read back
different", PDC 0x101 -> 0x80000001, "HFI IS UP" (bit 0 already set). Step 17: ARC counts gfx 10 /
cx 14 / mx 15; **perf table exactly as predicted** (gx1 0x380201/295000 .. gx9 0x1a00d09/940000,
gmu 0x400302/200000 0x800403/500000). cmd-db BCMs: SH0 addr 0x50004 unit 4200000 width 64 vcd 1;
MC0 0x50000 unit 3200000 width 16 vcd 0; ACV 0x5004c unit 1 width 0 vcd 11; CN0 0x50030 unit
140000 width 4 vcd 5 (all vcds differ: ddr wait mask 0x7, every command commits). DDR votes
(SH0/MC0/ACV): ddr0 all 0x40000000 (commit, invalid); ddr1 0x600002e8 0x600003d0 0x60000008 ...
ddr8 0x60001eff 0x600028af 0x60000008 (rechecked by hand); cnoc 0x40000000 / 0x60000001. HFI
memory 0xa24fc4000 through the uncached alias; GMU2HOST_INTR_MASK 0. **All six commands acked
with result 0 within 0.1 ms**: perf table (hdr 0x103b04, ack 0x10304), bw table (0x20a003, 160
words), IFPC/ACD/BCL off (0x30040b/0x40040b/0x50040b), core fw start (0x60020e, ack 0x51030e);
GMU2HOST_INTR_INFO bit 0 each time, command queue r = w after each, message queue +4 words per
ack (acks are 3 words, padded). No F2H error, no debug queue entry, no NMI, CB1 FSR 0x400, rscc
seq_busy 0, rpmh_power_state 0, gfx_pwr_clk_status 0x3; **GMU log 15 -> 94 words**. Undo: 40
entries; the new CM3 entry first (gbif halt ack in, sysreset 1), then PDC/RSCC back, step 15's
CM3 entry again, frees, stream 5 unrouted, CX down; "run over". System as #37 (agent up).
**Steps 1-17 proven: the GMU runs our tables and talks HFI.** NEXT: the GX power request (kgsl
after hfi_start: default votes, then gen7_gmu_oob_set(OOB_GPU) - the GMU turns GX on), then step
18 (3D core SMMU context, ring, SQE microcode).

**GPU STEP 18 = 0983 (2026-10-23, written, NOT built/booted), `kgpu=18`: GX on, slumber at the end.**
kgpu numbers now run one ahead of the port map: kgpu 18 = port map 17's second half (power the 3D
core); the port map's 18 (3D core SMMU context, ring, SQE) becomes kgpu 19. Read from kgsl:
gen7_hfi_start ends with kgsl_pwrctrl_setup_default_votes = gpu_clock_set(default_pwrlevel) ->
gen7_gmu_dcvs_set(perf idx = gpu_level_num - level - 1) and kgsl_pwrctrl_axi(on) ->
kgsl_bus_update(VOTE_ON): buslevel = min(pwrlevels[0].bus_max, pwrlevels[default].bus_freq), ab
= 25% of ib (MBps; goes to Linux icc, not the GMU, without gmu_ab) -> gen7_gmu_bus_set ->
dcvs_set; both as H2F_MSG_GX_BW_PERF_VOTE {hdr, ack_type 1, freq, bw} with bw = level |
0xffff0000 (adreno_gmu_bus_ab_quantize without gmu_ab). Bin 0xc6: initial-pwrlevel 7 (345 MHz),
bus-freq-ddr7 2, bus-max-ddr7 of level 0 = 8 -> perf {freq 2, bw 0xffff00ff}, bus {freq 0xff, bw
0xffff0002}. gen7_gpu_boot: kgsl_mmu_start, gen7_gmu_oob_set(oob_gpu) = HOST2GMU_INTR_SET BIT(30),
poll GMU2HOST_INTR_INFO BIT(31) 100 ms, clear BIT(31); then H2F_MSG_START (10), gen7_start,
rb_start ... oob_clear = HOST2GMU_INTR_SET BIT(31). Power off (gen7_power_off ->
gen7_gmu_power_off): wait_for_lowest_idle (RPMH_POWER_STATE == idle_level, 0 = ACTIVE without
IFPC), complete_rpmh_votes (RSCC TCS0..9 DRV0 STATUS bit 0, 2 ms), notify_slumber
(POWER_COUNTER_ENABLE 0x1f840 = 0, H2F_MSG_PREPARE_SLUMBER {hdr, bw, freq} = {.., 0xffff0002, 2},
AO_AHB_FENCE_CTRL 0, FALNEXT 1), wait_for_idle (AO_GPU_CX_BUSY_STATUS bit 30 clear, 100 ms),
rscc_sleep_sequence (CM3_SYSRESET 1, read GENERAL_9, RSCC_CONTROL_REQ 0x23b07 = 1, RSCC STATUS0
bit 16 within 100 ms, REQ 0), gen7_halt_gbif, irq off, clocks, CX off. 0983 does the votes, the
OOB set (undo: the OOB clear, then this power-off as a SLUMBER undo entry; GX GDSC collapsed from
our side if GX still reads on), reads RBBM_STATUS 0x210 / CP_STATUS_1 0x825 / CP2GMU_STATUS 0x812
only if GFX_PWR_CLK_STATUS bits 1:0 are 0. Step 17's HFI alias now stays until the undo (UNMAP
entry before its CM3 entry). Undo peaks at 43. H2F_MSG_START is left for kgpu 19. Success line:
"GX IS ON: the GMU powered the 3D core". Verified: 225 patches, series reproduces the file,
syntax clean. **Test:** #38 image with `kgpu=18`.

**BOOT #39 (2026-10-23 23:46, shim/out/shim-boot-39.img = #38 with `kgpu=18`, kernel CI 40003719080
@ c918ccb sha256 8f34a9e5...): STEP 18 PASSES, "GX IS ON: the GMU powered the 3D core"; THE SLUMBER
TAKES IT DOWN AGAIN; NO PANIC, UNDO CLEAN.** Log: temp/ios-m8/usblog/live-boot39-20261023-2346.txt.
Steps 1-17 as #38 (six HFI acks, result 0). Step 18: before, gfx_pwr_clk_status 0x3 (GDSC off,
clock off), GX GDSCR 0x22f001, rpmh_power_state 0, cx_busy 0x30000000. Default perf vote (hdr
0x70041e, ack 0x61031e) and bus vote (0x80041e, ack 0x71031e) both result 0 in 0.1-0.2 ms; GX
still off after them. **OOB_GPU acked** (GMU2HOST_INTR_INFO 0x80000000) -> **gfx_pwr_clk_status 0
(GDSC on, clock on), GX GDSCR 0xf8222800** (PWR_ON, collapse bit cleared by the GMU). GX register
reads fine: RBBM_STATUS 0x1, CP_STATUS_1 0, CP2GMU_STATUS 0 (no CP running). CB1 FSR 0x400, rscc
seq_busy 0; GMU log 290 words. Held 5 s with GX on. Undo (43 entries): oob_gpu clear; slumber:
rpmh_power_state 0, the 10 TCSes idle, **PREPARE_SLUMBER acked result 0** (hdr 0x900321) -> GX off
(0x3, GDSCR 0x222801), **rpmh_power_state 0xf (GPU_HW_SLUMBER)**; GPU idle; CM3 into reset (log
wptr 92); **RSC sleep done** (STATUS0 0x1010006: bit 16 set; RSCC_CONTROL_ACK 1); cx_busy
0x71000000 after it; the GX-collapse fallback was not needed. Then step 17's CM3 entry (GBIF halt
acked), the HFI alias unmapped, PDC/RSCC words back, step 15's CM3 entry, frees, stream 5 unrouted,
CX down: after undo cx gdsc 0x22f001, gx gdsc 0x222801 (off); "run over". System as #38 (agent
up). Cosmetic: kg.step is 0 during the undo, so the HFI lines sent from the slumber print step
"0". **Steps 1-18 proven: the GMU turns GX on and off for us.** NEXT (kgpu 19 = the port map's 18):
H2F_MSG_START after OOB_GPU as gen7_gpu_boot does, then the GPU's own SMMU context (stream 0 ->
an HLOS context bank with our tables, kgsl_mmu_start), the ringbuffer and a710_sqe.fw.

**GPU STEP 19 = 0984 (2026-10-23, written, NOT built/booted), `kgpu=19` (the port map's 18): the
GPU's SMMU context, ring, SQE microcode.** Read from kgsl: gen7_gpu_boot = kgsl_mmu_start,
oob_set(gpu), gen7_gmu_hfi_start_msg (H2F_MSG_START 10, hfi_start_cmd = header only),
gen7_start, ..., gen7_rb_start (ring memset 0xaa, RPTR_ADDR, RB_CNTL, RB_BASE, SQE_INSTR_BASE
0x830/0x831, CP_SQE_CNTL 0x808 = 1 starts the CP, then CP_INIT). adreno_get_firmware copies the
file minus its first dword into a GPUREADONLY global; get_ucode_version = word 1 (unless its low
nibble is 0xa). FDT kgsl-iommu: gfx3d_user iommus <0x1fb 0x0 0x400>, gfx3d_secure 0x2. a710_sqe.fw
(vendor, 36308 bytes, sha256 c5588585..., words 0 0x01710181 ...) **uploaded 2026-10-23 during
boot #39** to /usr/share/firmware/kunzite/a710_sqe.fw (0644), read back with /get = identical
(copy in temp/ios-m8/fw-readback/). 0984: H2F_MSG_START; stream 0 -> SMR0 (mask 0x400) -> S2CR0
-> CB0, programmed and kept like CB1, TTBR0 = the shared root with ASID 0; pool 8 -> 12 tables (the
GMU uses 7, the GPU's region at root entry 4 needs an L2 and an L3); ring 0x100000000 (32K),
scratch 0x100008000, SQE 0x100010000 (36304 bytes); TLBIASID 0 and 1 + sync; CP_RB_BASE and
CP_SQE_INSTR_BASE written and read back; CP_SQE_CNTL untouched. Undo: SMR0/S2CR0 back, CB0 off,
the three blocks freed, then step 18's OOB clear and slumber; peaks at 50. Teardown keeps kg.step
(undo lines carry 19, not 0). Success line: "THE GPU HAS ITS CONTEXT, RING AND SQE MICROCODE (the
CP stays halted)". Verified: 226 patches, series reproduces the file, syntax clean. **Test:** #39
image with `kgpu=19`.

**BOOT #40 (2026-10-24 00:03, shim/out/shim-boot-40.img = #39 with `kgpu=19`, kernel CI 40007469839
@ c950025 sha256 09e89eac...): KGPU 19 PASSES, "THE GPU HAS ITS CONTEXT, RING AND SQE MICROCODE (the
CP stays halted)"; NO PANIC, UNDO CLEAN.** Log: temp/ios-m8/usblog/live-boot40-20261024-0003.txt.
Steps 1-18 as #39 (GX on: gfx_pwr_clk_status 0, GX GDSCR 0xf8222800, RBBM_STATUS 0x1). Step 19
before: smr0 0, s2cr0 0x20000 (fault), cbar0 0, cb0 sctlr 0x70e (off, leftovers: tcr 0x5886538a,
ttbr0 0x2e01032f:40804030, mair0 0xf020241), stream 0 unmatched; CP_SQE_CNTL 0, SQE_INSTR_BASE
and RB_BASE 0. **H2F_MSG_START acked result 0** (hdr 0x90010a, ack 0x81030a). a710_sqe.fw read
from the root filesystem: 36308 bytes, version 0x1710181, words 0 0x1710181 0x1002374
0x1710149. GPU 0x100000000 (ring, 32K) -> 0xa2b248000, 0x100008000 (scratch) -> 0xa2b24c000,
0x100010000 (SQE, 9 pages) -> 0xa2b258000; **9 of 12 tables used** (7 + L2 + L3, as planned),
root 0xa24a4c000. CB0 programmed and kept (cba2r 1, cbar 0x1f300, tcr2 0x38011, tcr 0x803519,
ttbr0 0xa:24a4c000, mair0 0xf404ff44); TLB syncs on CB0 and CB1 done; CB0 on (sctlr reads 0x27,
as CB1 does), S2CR0 0 (translate, CB0), **SMR0 0x84000000** (valid, mask 0x400, stream 0); fsr
0x400 sgfsr 0. **CP_RB_BASE 0x1:00000000 and CP_SQE_INSTR_BASE 0x1:00010000 read back**;
CP_SQE_CNTL 0; RBBM_STATUS 0x1; CB0 and CB1 FSR 0x400 (no fault). Undo (50 entries): smr0 0,
s2cr0 0x20000, cb0 sctlr 0, the three blocks freed, then oob_gpu clear and the slumber
(PREPARE_SLUMBER acked, GX off, rpmh_power_state 0xf, RSC sleep done), CM3s, alias unmapped,
RSC/PDC back, three table pages freed, CX down; "run over". The undo's lines now carry step 19
(kg.step kept). System as #39 (agent up). **The port map's 1-18 proven (kgpu 1-19).** NEXT
(kgpu 20 = the port map's 19-20): the zap shader loaded through TrustZone (PAS mem setup + auth
and reset of a710_zap.mdt/.bNN into its region 0x89e1a000), then gen7_start's registers and
gen7_rb_start: RPTR_ADDR, RB_CNTL, CP_SQE_CNTL 1 and CP_INIT - the CP fetching the microcode
through context 0 is the proof of the GPU's translation.

**GPU STEP 20 = 0985 (2026-10-24, written, NOT built/booted), `kgpu=20` (the port map's 19-20): zap
shader, gen7_start, CP start.** Read from kgsl: kgsl_zap_shader_load = request a710_zap.mdt,
qcom_mdt_get_size (max - min of the loadable segments, 4K-aligned), memremap the zap-shader
memory-region WC, qcom_mdt_load (pas_init: INIT_IMAGE + MEM_SETUP(pas, phys, size) when
relocatable; each segment copied from the .mdt or "<name>.bNN"; memsz-filesz zeroed),
PAS_AUTH_AND_RESET(13). a710_zap.mdt: 3 phdrs; only #1 loads (PT_LOAD, paddr 0x1000, 0x79c,
flags 0x8000007 = relocatable) -> MEM_SETUP(13, 0x89e1a000, 0x1000), #1 from a710_zap.b01 (1948
bytes, sha256 34e1cda9..., **uploaded 2026-10-24 during boot #40**, read back identical; copy in
temp/ios-m8/fw-readback/). gen7_start (gen7_3_0: no_cb family, hbb 15, mal 32 from the FDT, UBWC
4.0, uche_gmem_base 16M, uche_client_pf 1, hwcg on) and gen7_rb_start transcribed as in the patch
header; the hwcg and protect tables were compared with kgsl's by script (49 and 46 entries,
equal). **kgsl's trusted range** (_set_secvid with a 64-bit MMU: base 0xf0000000, size
0xfffff000) covers 0x100000000, so the GPU's addresses move to 0xe0000000 (kgsl's own globals
sit just under 0xf0000000). CP packets: CP_THREAD_CONTROL = 0x70970001 (BIT(27)), CP_ME_INIT =
0x70c80007 + mask 0x94b, 3, 0x20000000, 2, reglist lo/hi (empty, scratch +0x800), BIT(31); WPTR
10; RB_CNTL 0x20c. Risks in order: the zap region write (0x89e1a000 no-map: if Gunyah does not
give it to HLOS, an external abort right after "the zap segments into the region"); TrustZone
refusing MEM_SETUP after step 12's INIT_IMAGE; the CP faulting through context 0 (CB0
FSR/FAR printed). Undo peaks at 51: CP_SQE_CNTL 0 first. Success lines: "THE ZAP SHADER IS
LOADED (TrustZone authenticated it)" then "THE CP RUNS: CP_ME_INIT consumed, the GPU idle
(microcode fetched through context 0)". Verified: 227 patches, series reproduces the file, syntax
clean. **Test:** #40 image with `kgpu=20`.

**BOOT #41 (2026-10-24 00:27, shim/out/shim-boot-41.img = #40 with `kgpu=20`, kernel CI 40010709615
@ 9e7fe65 sha256 c0095ab9...): KGPU 20 PASSES: "THE ZAP SHADER IS LOADED (TrustZone authenticated
it)" AND "THE CP RUNS: CP_ME_INIT consumed, the GPU idle (microcode fetched through context 0)";
NO PANIC, UNDO CLEAN.** Log: temp/ios-m8/usblog/live-boot41-20261024-0027.txt. Steps 1-19 as
#40, the GPU's addresses now 0xe0000000 (ring 0xe0000000, scratch 0xe0008000, SQE 0xe0010000;
9 of 12 tables; CB0 before: sctlr 0x30e, other leftovers than #40's). Zap: loadable 0x1000..0x2000
relocatable; **PAS_MEM_SETUP(13, 0x89e1a000, 0x1000) ret 0 res 0** (step 12's INIT_IMAGE still
holds); **the zap region is writable from HLOS** (1948 bytes of a710_zap.b01 written, no abort);
**PAS_AUTH_AND_RESET(13) ret 0 res 0**. gen7_start: 20 plain writes, ao hwcg, 46 protect, secvid,
49 hwcg, RBBM_CLOCK_CNTL 0x8aa8aa82 (was 0); 5 registers read back other than written:
RBBM_PERFCTR_GPU_BUSY_MASKED 0x50b reads 0x2f7fff (writable bits only), UCHE_GMEM_RANGE_MIN_LO
0xe0b and MAX_LO 0xe0d read 0 (not kept on gen7_3_0?), RBBM_CLOCK_MODE_CP 0x260 and
RBBM_ISDB_CNT 0x533 read 0 - kgsl writes them blind; none mattered here. RB_CMP_DBG_ECO_CNTL
0x4000 -> 0x4800. rb_start: RPTR_ADDR 0xe0008000, RB_CNTL 0x20c, **CP_SQE_CNTL 1**: rbbm_status
0x1, no CP interrupt, no fault. **WPTR 10 -> RPTR 10 at once ("after 0.0 ms"), the shadow in the
scratch page reads 10 (the CP wrote it through context 0), CX busy 0x71000000 (bit 23 clear),
SQE_STAT 0x70c80007 (the CP_ME_INIT header), CB0 FSR 0x400, SGFSR 0.** So the GPU's CP fetched
its microcode and the ring and wrote memory, all through our tables. Undo (51): cp_sqe_cntl 0
first, stream 0 unrouted, CB0 off, frees, oob clear, slumber (PREPARE_SLUMBER acked, GX off,
0xf, RSC sleep done), the rest as #40; "run over". System as #40 (agent up). **The port map's
1-20 proven (kgpu 1-20).** NEXT (kgpu 21 = the port map's 21): out of secure mode
(CP_SET_SECURE_MODE 0 through the ring, as gen7_rb_start does once the zap is loaded) and a word
written by the GPU (CP_MEM_WRITE to the scratch page), both with WPTR/RPTR and idle checks.

**GPU STEP 21 = 0986 (2026-10-24, written, NOT built/booted), `kgpu=21` (the port map's 21): out of
secure mode, a word written by the GPU.** Read from kgsl: gen7_rb_start after
adreno_zap_shader_load: zap loaded -> CP_SET_SECURE_MODE (0x66, 1 dword, payload 0) through the
ring + gen7_ringbuffer_submit + adreno_spin_idle(2000); without a zap it writes
RBBM_SECVID_TRUST_CNTL (0xf400) = 0. CP_MEM_WRITE (0x3d) on gen7 = 3 dwords lo, hi, value.
Packets: 0x70e60001 0; 0x703d8003 0xe0008100 0 0x6b677021, then CP_WAIT_MEM_WRITES (0x12, 0
dwords) 0x70928000. Ring after step 20: WPTR = RPTR = 10; these go to words 10-11 (WPTR 12) and
12-16 (WPTR 17). Shared submit helper: append at WPTR, clean, WPTR, poll RPTR = WPTR and CX busy
bit 23 clear for 2 s, print CP/RBBM/CB0 status. The word is read back after a cache invalidate of
the scratch page. Nothing new to undo (step 20's CP halt runs first). Success line: "THE GPU WROTE
A WORD: 0x6b677021 at 0xe0008100, out of secure mode". Verified: 228 patches, series reproduces
the file, syntax clean. **Test:** #41 image with `kgpu=21`.

**BOOT #42 (2026-10-24 00:47, shim/out/shim-boot-42.img = #41 with `kgpu=21`, kernel CI 40012827112
@ 3cfdc6f sha256 1caabb21...): KGPU 21 PASSES, "THE GPU WROTE A WORD: 0x6b677021 at 0xe0008100, out
of secure mode"; NO PANIC, UNDO CLEAN.** Log: temp/ios-m8/usblog/live-boot42-20261024-0047.txt (the
boot #41 listener was still running and held the UDP port, so the first #42 listener exited;
stopped it and started a new one, which got the full replay from the kernel's first line;
live-boot41's file also holds lines received after #41's run, up to 00:48). Steps 1-20 as #41
(zap MEM_SETUP and AUTH_AND_RESET ret 0, CP_ME_INIT consumed at once). Step 21: before,
**RBBM_SECVID_TRUST_CNTL 0x1** (secure), scratch rptr shadow 10, word 0. CP_SET_SECURE_MODE 0:
WPTR 0xa -> 0xc, RPTR 12 after 0.1 ms, idle, no CP interrupt/fault, SQE_STAT 0x70e60001 (the
packet), CB0 FSR 0x400; **RBBM_SECVID_TRUST_CNTL 0 after: out of secure mode** (through the
zap shader, as kgsl). CP_MEM_WRITE + CP_WAIT_MEM_WRITES: WPTR 0xc -> 0x11, RPTR 17 at once,
idle, SQE_STAT 0x70928000; **the word at 0xe0008100 reads 0x6b677021** (the rptr shadow 17). Undo
(51): CP halted, stream 0 unrouted, frees, oob clear, slumber (acked, GX off, 0xf, RSC sleep
done), the rest; "run over". System as #41 (agent up). **The port map's 1-21 proven (kgpu 1-21):
the GPU runs commands and writes memory, out of secure mode, on this kernel under Gunyah.**
NEXT (kgpu 22 = the port map's 22): a picture - the GPU fills a buffer (CP_MEM_WRITE runs, or a
blit/2D clear) that ends up on the splash framebuffer (0xb8000000), as hk did on redwood.

**GPU STEP 22 = 0987 (2026-10-24, written, NOT built/booted), `kgpu=22` (the port map's 22): a
picture.** hk's redwood_adreno.c step 22 (temp/kunzite-research/hackios-xnu-kernel/.../
redwood_adreno.c: 2D solid fill x3 with the a6xx register names, CP_BLIT, flushes, then
MiniIOMobileFramebufferShow). For a7xx the register database and Mesa's a7xx paths were fetched to
temp/gpu-plan-review/regdb-2026-10-24/ (Linux a6xx.xml, adreno_pm4.xml, a6xx_enums.xml,
adreno_common.xml; Mesa fd6_emit.h/.cc, fd6_gmem.cc, freedreno_devices.py, fd6_gmem_cache.h,
freedreno_gpu_event.h). a7xx differences from hk's a6xx code: SP_A2D_OUTPUT_INFO at 0xa9bf (not
0xacc0); TPL1_A2D_BLT_CNTL 0xb2d2 (type 2D) is new; RB_CCU_CNTL lost its cache fields to
RB_CCU_CACHE_CNTL 0x88e5; events are CP_EVENT_WRITE7 (0x46) with CCU_CLEAN_COLOR 0x21,
CACHE_CLEAN 0x31, CCU_INVALIDATE_COLOR 0x19, DEBUG_LABEL 0x3f (Mesa's FD_LABEL before CP_BLIT);
the a2d IFMT field is 3 bits with R2D_UNORM8 = 0 (hk's 0x10 << 24 is the older 5-bit encoding).
Mesa's FD710 (chip 0x07010000): a7xx_base + a7xx_gen1, num_ccu 1, hbb 15; sysmem CCU: depth 256K
at 0, colour 64K at 256K -> RB_CCU_CACHE_CNTL 0x20000000. Mesa also writes a710_raw_magic_regs
at context setup; hk did not on a6xx and 0987 does not either (first thing to add if the blit
hangs or draws garbage). The panel: AppleCLCD 1080x2400 stride 4320 at 0xb8000000, scanned out
directly (VIG0/CTL0 unmapped on kunzite, no flipping); 4320 is not a multiple of 64 (the 2D
pitch's unit), so the GPU draws into rows of 4352 and the CPU copies. Pool 16 tables (14 used
expected). Undo peaks at 53. Success lines: "22 ... 0 of N samples wrong" and "A PICTURE DRAWN
BY THE GPU IS ON THE PANEL: orange, white, blue from the top (held 15 s)" - **watch or film the
panel during those 15 s**. Verified: 229 patches, series reproduces the file, syntax clean.
**Test:** #42 image with `kgpu=22`.

**BOOT #43 (2026-10-24 01:11, shim/out/shim-boot-43.img = #42 with `kgpu=22`, kernel CI 40016172124
@ 48a171d sha256 c1c60d13...): STEP 22 FAILS CLEANLY - THE 2D WRITES TAKE A PERMISSION FAULT; NO
PANIC, UNDO CLEAN.** Log: temp/ios-m8/usblog/live-boot43-20261024-0111.txt. Steps 1-21 as #42
(out of secure mode, the word written). Step 22: picture 0xe0200000..0xe0bf6000 -> 0xa3324c000, 14
of 16 tables used (as planned); 172 words, WPTR 0x11 -> 0xbd; RPTR 189 after 0.9 ms, CX busy bit
23 clear, RBBM_STATUS 0x1, no RBBM/CP interrupt, no HW fault, no protect fault, SQE_STAT
0x70928000 (the last packet, CP_WAIT_MEM_WRITES, was reached) - but **CB0 FSR 0x408 = PF
(permission fault)**, every pixel and the marker 0 (144 of 144 samples wrong), so nothing was
copied to the panel. Undo ran clean ("gpu picture: freed 0x9f8000", CP halted, slumber, CX down).
**Cause:** our PTEs (PTE_GMU) have AP[2:1] = 00 = privileged only. The CP's own accesses are
privileged (CP_APRIV_CNTL 0x6f: ring fetch, RPTR writeback, ICACHE/SQE, ...; step 21's
CP_MEM_WRITE got through), but the 2D engine's colour writes (RB/CCU via UCHE) are unprivileged,
so each faulted. **And the marker was lost because CB0 SCTLR.HUPCF (bit 8) is 0**: with a fault
outstanding and CFCFG = terminate, the SMMU terminates every later transaction of the context
until FSR is cleared. Linux's arm-smmu-qcom sets HUPCF on the Adreno context ("process subsequent
transactions after a fault to keep the GPU from hanging"); kgsl maps its globals (ring, scratch,
ucode) privileged and everything else unprivileged (io-pgtable-arm AP_UNPRIV, bit 6, without
IOMMU_PRIV). **FIX (0988, to write):** the picture mapped with AP[1] = 1 (unprivileged RW); CB0
SCTLR with HUPCF; kg_cp_submit clears CB0 FSR before each submit and prints FAR/FSYNR0 when a
fault bit is set (this boot had no FAR to say which address).

**FIX 0988 (2026-10-24, written, NOT built/booted), still `kgpu=22`.** kg_gpu_alloc takes the PTE
attributes: ring, scratch page and SQE keep PTE_GMU (privileged only, like kgsl's privileged
globals); the picture is mapped PTE_GMU | PTE_AP_UNPRIV (bit 6, AP[1] = 1: unprivileged RW too, as
io-pgtable-arm maps without IOMMU_PRIV). CB0's SCTLR is written SMMU_SCTLR_ON | HUPCF (bit 8),
as arm-smmu-qcom does for the Adreno context (expect the readback 0x127 instead of #42's 0x27,
if HUPCF sticks). kg_cp_submit clears CB0 FSR (write-1s) before moving WPTR and prints "CONTEXT 0
FAULT: fsr far fsynr0" when a fault bit is set (FSYNR0 per Linux arm-smmu.h: bit 4 WNR, bit 5
PNU unprivileged, bit 6 IND instruction). Step 22's sequence is otherwise unchanged. If the
fault persists, FAR says which address, FSYNR0 whether it was a write and unprivileged; the
next suspects are then the 2D engine's other paths (Mesa's a710 magic registers). Verified: 230
patches, series reproduces the file, syntax clean. **Test:** #43 image with `kgpu=22` (watch the
panel ~75-90 s after boot).

**BOOT #44 (2026-10-24 01:26, shim/out/shim-boot-44.img = #43's with the 0988 kernel, CI 40018271036
@ 9f70363 sha256 ab47fe4a...): THE GPU DRAWS A PICTURE AND IT IS ON THE PANEL (the user saw the
stripes); THEN A KERNEL PANIC FROM OUR OWN UNMAP.** Log: temp/ios-m8/usblog/live-boot44-20261024-
0126.txt (ends inside the 15 s show). Step 19: CB0 SCTLR written 0x1127, **reads 0x127: HUPCF
kept** (ASIDPNE still not). Step 22: picture mapped "unprivileged too", 14 of 16 tables; 2D
stream consumed in 0.9 ms, idle, no interrupt, **CB0 FSR 0x400 (no fault)**, **marker
0x6b677022, first pixel 00 80 ff ff (orange), middle ff ff ff ff (white), last row ff 80 00 ff
(blue), 0 of 144 samples wrong**; "A PICTURE DRAWN BY THE GPU IS ON THE PANEL: orange, white,
blue from the top (held 15 s)". **Milestone K2 of the port map: the GPU's 2D engine draws through
our SMMU tables, out of secure mode, and the picture is shown.** The panic, after the show (the
USB log stops; **confirmed by the panic screen**, user's transcription: "panic(cpu 1 caller
0xfffffe0007498588): pmap_remove_options() pmap 0xfffffe0007c49738 start 0x...d955000 end
0x...f33400 @pmap.c:4296", panicked thread tid 206 in kernel_task, the other cores idle):
ml_io_unmap(panel, 4320 * 2400) - its
pmap_remove(addr, addr + 0x9e3400) panics in pmap_remove_options when the end is not page
aligned (osfmk/arm/pmap/pmap.c: "pmap_remove_options() pmap %p start 0x%llx end 0x%llx");
ml_io_map_unmappable had rounded the mapping up to 0x9e4000 (kmem_free rounds too; pmap_remove
does not). Step 17's HFI alias (0x4000) was already page sized. **FIX (0989, to write):** map and
unmap the panel alias with the size rounded to PAGE_SIZE. Nothing of the GPU's undo ran this boot;
the reboot reset it all.

**What to expect (read from the merged code before boot #25; boots #25-#31 above are what was seen):**
- Standard cmdline (`sweetfb_stayon=1 sweetfb_hz=30`): should look like #24. 0948 now tells
  CoreAnimation 60 Hz (before: frames paced 50 ms apart), but with sweetfb_hz=30 the panel still
  gets every 2nd frame (fFlushTicks=2). Try `sweetfb_hz=60` to see it (twice the CPU copy work).
- Without sweetfb_stayon: display off now fades the splash fb to black in place (0143 removed
  the "fDsi mapped -> keep the last frame" return; kunzite has no fFlip buffers since fVig0=NULL).
  This is NOT panel sleep: dsiSend no-ops (fDsiBuf needs memory below 4 GiB, kernel RAM is all
  above), and upstream's own 0143/0152 comments say the DCS commands do not reach the topaz
  panel yet (video mode).
- **Keep OFF on kunzite** (khaje addresses inside): `gpu=1` (GPU pool at phys 0x90000000,
  TopazAdreno MMIO), `adsp=1` (carveout 0x53800000, LPASS 0x0a600000), `modem=1` (also starts
  0900's wifi/en0), `sweetbattery=1` (0148 now also WRITES the bq2589x charger at I2C 0x6a; it
  only read before). Checked in code: all gated, default off. TOPAZ_4K is unset everywhere ->
  16K pages; AMP (0141) is inert.

**Then:** scout plan step (2) is still OPEN: the t02 splash-tail DCS buffer is not on this branch,
so no DCS command reaches the panel. Port it (0143 already sets LP mode, bit 26) before expecting
panel sleep. Then step (3) redwood_adreno.c -> parrot a710 (re-derive the GPU pool base for
kunzite's memory map first), then (4) userland. The t01-t03 branches are based on pre-merge
69446d0 and their 0911-0913 collide with upstream's 0911-0913: renumber when rebasing.

**Tooling (2026-10-23):** tools/build-local.sh hard-codes /Applications/Xcode_15.4.app; this Mac
only has Xcode 27.0, so CI is the only way to build the kernel until 15.4 is installed. `gh` is not
installed either; run status works through the public REST API with curl.

## MORNING HANDOFF (2026-10-21, 02:30) - kunzite-agent open issues (superseded; all six issues FIXED in 8e4cb88, see "AGENT review fixes" near the end)

**Where things stood (superseded by boot #24 below):** boot #23 proved USB Ethernet + live console (0909). 0910 (log replay on
request, paced sends) is committed but NOT yet CI-built: the last kernel artifact is d5f0c370
(0909 only). The agent (agent/kunzite-agent.c, doc: rtk-tablet `ios/kunzite-agent.md`) is built
but NOT installed in the root image yet (checked 2026-10-21: no /usr/libexec/kunzite-agent, no
com.kunzite.agent in the launchd.plist cache).

**2026-10-21/22:** the phone ran a whole day on battery on boot #23's kernel without visible
crashes (user); afterwards 3 % left, charging before the agent flash (do not flash userdata on a
near-empty battery; whether our kernel charges at all is unknown - SweetBattery is static, so
charge powered off or in the bootloader).

**BOOT #24 (2026-10-22 02:37, shim-boot-23.img on the NEW userdata with the agent): AGENT WORKS.**
Flash: `fastboot flash userdata temp/ios-m8/ios16-root-20H392-agent.simg`, 37 sparse pieces, 324 s,
all OKAY. `fastboot boot` then took 102.7 s to say OKAY (usual 0.3 s; ABL busy after the big write?
the boot itself was normal). USB link 28 s later; `curl http://192.168.7.2:8022/ping` ->
`kunzite-agent 1 pid 90 uid 0` 4 s after that. launchd honours StandardOutPath=/dev/console: the
agent's own lines ("on tcp/8022 ...", "GET /ping from 192.168.7.1") are in the live USB log
(temp/ios-m8/usblog/live-boot24-*.txt). /ps: 156 processes incl. SpringBoard (uid 501),
backboardd, logd; CrashReporter dir empty (fresh userdata); the live log shows only driverkitd
crashes during boot. Live log listener: `tools/udp-log.py FILE --no-replay` (kernel d5f0c370 has no
0910 replay yet).

**GRAPHICS OVERNIGHT (2026-10-22, user asleep): three tier branches, none pushed/built.**
Plan + rationale: main repo ios/gpu-plan.md (private). Branches off kunzite 69446d0, each +1 patch,
series-verified (152 applied):
- `t01-panel-diag` (3230b0d, 0911): read-only dump of the running display pipe (DSI0 ctrl,
  SDE ctl0/vig0/lm0/intf_1/tear/pp0), sweetpaneldiag=1 default. **PUSH THIS FIRST** - its dump
  picks the DCS registers (0912 sanity gate) and the flip register set (0913).
- `t02-screenoff` (57e4e0e, 0912): topaz's DCS path made real - packet page at the splash tail
  (fPhys + 0x2b00000 - 0x1000; kernel RAM is all >4 GiB so the <4G buffer never allocated and
  every DCS write silently no-oped). Off/on 0x28/0x29 only with `sweetfb_off=1` (stayon stays the
  outer gate; boot #15's reset was such a write). Brightness 0x51 behind sweetbl as before.
- `t03-dpu-flip` (a79cc59, 0913): skeleton, `sweetfb_flip=1` maps the MDP window and read-only
  probes SSPP0 during swaps; second scanout buffer = fPhys + 0x1000000. No MDP writes yet.
Boot order: t01 first (any boot, default cmdline; read SWEET DIAG lines in the USB log), then t02
(boot WITHOUT sweetfb_stayon, WITH sweetfb_off=1: expect panel off on lock; recovery = reboot),
t03 after. T0.1 FINDING (agent, phone): **the "black wallpaper" is no wallpaper at all**
(springboard.plist SBHome/LockScreenWallpapers = 'none', kSBProceduralWallpaperHomeUserSetKey
False) - set one in Settings to close that mystery; the red-until-wake tint is still open.
shim-boot-25.img (panic region) is unbuilt-by-CI but ready to boot any time.

**HackiOS OPEN-SOURCED (2026-10-22) - the GPU path is public.** github.com/oosexgang/hackios-xnu-kernel
(APSL 2.0; device "redwood", Adreno 642L, includes redwood_adreno.c + docs/PORTING.md) and
hackios-xnu-userland (MIT; Mesa freedreno behind CoreAnimation's GL renderer, ElleKit injection).
Upstream topaz merged from it 2026-10-22 (GPU, AMP all-core, sound, panel sleep, wallpapers,
widgets, tweak injection; their patches 0140-0152 + 0948/0949 display modes 60 Hz, 0950 touch).
Cloned to temp/kunzite-research/hackios-xnu-{kernel,userland}. Our gpu-plan Tier structure is
superseded: proven path = CA GL renderer <- Mesa <- kernel Adreno driver; our 0911-0913 branches
overlap their panel-sleep/display work. Scout workflow running (porting, adreno ABI, userland,
topaz delta). Lilu question answered: the lineage's "Lilu" is ElleKit (userland tweak injection).

**UPSTREAM GPU MERGE (2026-10-23, branch kunzite-topaz-gpu @ 6cdf249 - re-created from bf2c963, same
tree; bf2c963 is on no branch):** upstream
topaz e477291 (the HackiOS open-source batch: 0140-0152 + 0900-0950 + userland/{gpu,mesa,audio})
merged cleanly; our patches renumbered to **0960-0969**; 0960/0964/0967/0968 regenerated over the
merged tree. Series verified: 215 patches apply. TOPAZ_4K unset -> 16K pages + our 0961 topology
unchanged; AMP inert (inside the TOPAZ_4K ifdef); khaje-hw patches (wifi 0900, audio 0144-0146,
charger 0148) merged but dormant until re-ported. NEXT: push kunzite-topaz-gpu -> CI -> boot #25
(standard cmdline; expect: no change visible except faster frame clock from 0948, panel sleep now
possible if stayon removed + their LP-mode DCS, GPU still off). THEN: GPU port plan step 3
(redwood_adreno.c -> parrot a710) + userland (Mesa chipid, eagl slots for 20H392).
[CORRECTED 2026-10-23, see the top handoff: no panel sleep on kunzite (DCS no-ops, display off =
fade to black); the 60 Hz clock shows only with sweetfb_hz=60; step (2) comes before step 3.]

**CI run 39919704364 (kunzite-topaz-gpu @ 94bfddc) FAILED, 1 compile error (make -k, nothing else
broke):** `sweet_ufs.c:843 call to undeclared function 'kunzite_touch_init'`. Cause: regenerating
0908 -> 0967 dropped its first sweet_ufs.c hunk (`extern void sweet_touch_init` -> `kunzite_touch_init`
at line 54); the old hunk's context still matched. Restored verbatim; series re-applied to
sweet_ufs.c/sweet_keys.c/kunzite_touch.c cleanly (scratch git apply). 0960/0964/0968 regen diffs
reviewed: only context moves, no lost hunks. Externs used by 0960-0969 all defined in the series.
Blacktop's own 4 skipped patches (TrustCache, iobuffermemd, skywalk, syntax_checker) are dxb's, not ours: unrelated to the merge.
Pushed as 54b61da; rerun = CI 39923346510 SUCCEEDED (links, xnu-kernel 61 MB).

**SCOUT RESULT (2026-10-23, workflow wf_edc5205d, 4 readers):**
- HackiOS boot = fastboot RAM-boot -> BootShim + Mu-Silicium UEFI (drivers extracted from the
  phone's own xbl.img!) -> xnuloader EFI app -> XNU; one kernel for all devices (BCM2837 config
  base); device = .conf + platform.c (auto-generated by common/new-device.sh from the phone DTB)
  + redwood_*.c drivers; xl = resident USB loader (can park a 2.5 GB APFS root in RAM).
- GPU KERNEL: redwood_adreno.c (4125 lines): 22-step bring-up (gpucc, CX GDSC, GMU fw load,
  SMMUv2 with own PT walker, RSCC/PDC, HFI, TZ zap, CP_ME_INIT) + runtime (single ringbuffer,
  softpin VAs, polled fences, NO interrupts). **Userspace ABI = IOKit "RedwoodGPUUserClient"
  whose selectors ARE Linux msm DRM ioctl numbers** (0x40-0x4c) with msm_drm.h structs; a fake
  libdrm (tools/mesa/redwood-drm) bridges Mesa -> IOConnectCallStructMethod; IOSurfaces imported
  via selector 0x7e. **The whole ABI is GPU-generation-agnostic - reusable as-is for the 710.**
- USERLAND: stock Mesa 25.0.7 (softpipe+freedreno, one ~15 MB libOSMesa dylib, iPhoneOS16.5 SDK);
  CA hookup = kernel injects libredwood.dylib into backboardd (sysctl kern.exec_inject), it
  EMPTIES Metal's _deviceArray so CA falls to the EAGL/GL road, swizzles 8 EAGLContext methods
  and serves a generated vtable (mkeagl.py; slot offsets RE'd from 20F66 - MUST be re-derived
  for our 20H392). ElleKit 0.4.5 = hooking engine in a Procursus bootstrap. Mesa 25.0.7 freedreno
  HAS a7xx (FD730/740/750) but NO 710 chipid entry - we add 0x07010000 + verify a7xx paths.
  redwood GPU fw = a660_gmu/sqe/zap from the phone's vendor (never in repo); ours = gmu_gen70000.
- TOPAZ DELTA d8c82d9..origin/topaz: 4 commits, +22053 lines, 64 patches (HackiOS 0140-0152 +
  topaz chain 0900-0950 - NUMBERS COLLIDE with our 0901-0913! [resolved: ours are now 0960-0969;
  the unmerged t01-t03 branches still use 0911-0913]). Collisions with ours:
  SweetFramebuffer.cpp (their 0143/0144/0151/0152/09xx vs our 0905/0911/0912/0913 - same hunks),
  bsd/conf/files, SweetBattery, SWEET.h. THEIR PANEL SLEEP (0143+0152) SUGGESTS AN ANSWER TO OUR
  BOOT #15 MYSTERY: DSI commands in LOW-POWER mode (bit 26 SET; we cleared it for HS); off =
  0x51/0x28/0x10, on = 0x11 + 120 ms + 0x29. [HYPOTHESIS, 2026-10-23: upstream's own 0143/0152
  comments say these DCS commands do not reach the topaz panel yet (video mode); their display
  off is one black frame. LP mode is what Linux does, not proven here.] Their 0948 (publishModes 60 Hz + sel 79 block 0x40,
  fixes CA frame clock, was 20 fps) is device-independent - adopt verbatim. Their GPU core is
  khaje/Adreno-610 (NO GMU) - the parrot core (a7xx+GMU, closer to redwood's 642L) is ours to
  write from redwood_adreno.c.
- PLAN: (1) merge upstream tip into kunzite, renumber ours >= 0960; (2) reconcile framebuffer
  patches: keep our splash-tail DCS buffer + guards, adopt LP-mode + their sleep sequence + 0948;
  (3) port redwood_adreno.c -> parrot a710 (SMMUv2 0x3da0000, GPU 0x3d00000, GMU 0x3d68000, fw
  gmu_gen70000.bin + a7xx sqe/zap); (4) userland: Mesa +chipid, re-derive eagl slots for 20H392,
  adapt their root.apfs//redwood layout to our HFS+ image (agent-uploadable). Our t01/t02/t03
  branches: t01 (diag) still useful; t02 superseded by 0143+0152+LP; t03 largely by 0948+GPU.

**ROADMAP FACTS (2026-10-22, live FDT/dmesg):** battery = `qcom,battery-charger` under
`qcom,pmic_glink` (ADSP firmware owns gauge/charger; a proper driver needs ADSP PAS boot + GLINK +
battmgr protocol; quick option = estimate from raw PMIC values over SPMI if readable; open: does
the phone charge under our kernel at all). WLAN = `qcom,icnss` (membase 0x22800000), fw
WLAN.HL.3.4.3 on the modem Q6, wlan_fw_region 0x82a00000; BT node `bt_wcn3990` -> same Helium
family as topaz's WCN3988: TopazModem (0113-0130: modem PAS boot, GLINK/QRTR, WLFW, CE/HTC/WMI/HTT,
WPA2) is a real template; differences: GLINK signalling via `qcom,ipcc` mailboxes (topaz: APCS
doorbell bit 12) and all addresses. GPU = Adreno 710 (`qcom,adreno-gpu-gen7-3-0`, chipid
0x07010000, kgsl 0x3d00000, GMU fw gmu_gen70000.bin) - topaz's GPU path (Adreno 610 + HackiOS
patches, unpublished [published 2026-10-22, merged on kunzite-topaz-gpu]) does not carry over directly. Bluetooth: iOS bluetoothd expects Apple's
controllers/transport - last. Suggested order: red tint + real screen-off (agent makes the tint
cheap to investigate; screen-off = port topaz 0104-0110 DCS to parrot DSI0) -> quick battery
estimate -> Wi-Fi port (proper battery after it) -> GPU when HackiOS publishes -> Bluetooth.

**Panic region (2026-10-22, from sweet 1664d63):** mkadt now carves 1 MiB off the top of the
highmem window (/pram at 0xa7ff00000, /chosen/embedded-panic-log-size, dram-size shrunk) - without
it XNU's panic-log init can hang after "kdp_core zlib memory" (sweet: reproduced on QEMU and
ios-fajita), and it is where a panic-log reader (shim, after reboot) will look. Boot image
shim-boot-25.img = #23/#24 payload (same kernel d5f0c370, sweetusb=2) + the new kunzite.adt.
**GPU plan (WIP, private):** the main repo (local-only) has ios/gpu-plan.md; confirmed upstream
sweet = "HackiOS" (hackiosx.dev) has NO GPU patches - their future page calls it "maybe... the
hardest item by far". [OUTDATED 2026-10-23: HackiOS published its GPU work on 2026-10-22 (see
above). CREDITS.md names HackiOS as OOSEX GANG's POCO X5 Pro (redwood) port and lists ios-sweet
(dipa-studio) separately, so "upstream sweet = HackiOS" looks wrong; where the hackiosx.dev link
came from is unverified.]

**Agent image ready (2026-10-22 02:10):** agent built from 8e4cb88 (sha256 12dbe7c7...), installed
by `sudo tools/root-add-agent-mac.sh` into temp/ios-m8/ios16-root-20H392.img (checked read-only:
/usr/libexec/kunzite-agent 0:0 0755 same hash, com.kunzite.agent in launchd.plist, 531 daemons);
sparse image temp/ios-m8/ios16-root-20H392-agent.simg VERIFIED identical (205 chunks). The old
ios16-root-20H392.simg (no agent) is the fallback. Flash: `fastboot flash userdata
temp/ios-m8/ios16-root-20H392-agent.simg` (no -S), then boot #24 = shim-boot-23.img (kernel
d5f0c370, sweetusb=2) and `curl http://192.168.7.2:8022/ping`. Mac disk is down to 8.5 GiB free.

**Fixed tonight (commit c68e324):** `/get` on a FIFO wedged the single-threaded agent forever
(open() blocked waiting for a writer; SO_RCVTIMEO/SO_SNDTIMEO do not help there). Now /get opens
O_NONBLOCK and serves regular files only, /ls requires a directory, /put's temp file must be a
plain file (O_NOFOLLOW). Verified on the Mac build: /get and /ls on a FIFO answer ERR, /ping
answers afterwards, plain files download as before.

**Open issues [ALL SIX FIXED in 8e4cb88, 2026-10-21 - see "AGENT review fixes" near the end]
(all in h_run unless noted; reproduce with agent/build.sh mac, then run
agent/out/kunzite-agent.mac and curl 127.0.0.1:8022):**
1. **t= timeout does not stop the program.** `curl 'http://127.0.0.1:8022/run?a=/bin/sleep&a=20&t=2'`
   prints `[timeout]` but returns after 20 s: when the poll loop times out, the reaping loop only
   waits (its kill path is skipped once timed_out is already set). A program that never exits
   wedges the agent for good. Expected: back after ~2 s, the program gone.
2. **A vanished client is noticed only when the program writes.** Start
   `curl '.../run?a=/bin/sleep&a=15&t=0'`, kill that curl after 1 s, then `curl .../ping`: it
   answers only when sleep ends (14 s). The poll set has only the two pipes, not the client
   socket. Expected: /ping answers within ~1 s.
3. **No background mode** for long-running helpers (the planned os_log streamer would hold the
   agent for the whole stream through /run). Design question for the morning: how such a helper
   should be started and stopped, and where its output goes (the console = live USB log is the
   natural sink).
4. Minor: uploads over 1 MiB pause ~1 s (curl sends `Expect: 100-continue`, the agent never
   answers it; workaround `curl -H 'Expect:' -T ...`).
5. Minor: request log lines print a double slash (`GET //ping`): logf_ format "%s /%s" with a path
   that already starts with '/'.
6. Kernel, cosmetic: 0910-kunzite-usb-log-replay.patch has its main comment block pasted twice in
   sweet_usb.c (comment only; fix whenever 0910 is regenerated).

**Plan once the agent is settled (the user runs sudo/fastboot/GitHub):** push the fork + run CI
(0910) -> `agent/build.sh` -> `sudo tools/root-add-agent-mac.sh` -> `tools/img2simg.py` ->
`fastboot flash userdata` (DESTRUCTIVE: resets everything iOS saved on the phone since the last
flash) -> boot #24 = #23 cmdline (sweetusb=2) with the new kernel -> `tools/kunzite-usbnet-mac.sh`
-> `curl -s http://192.168.7.2:8022/ping`. Review notes from 2026-10-21: the binary is plain arm64,
minos 16.0, libSystem only, cryptid 0, ldid XML entitlements (sweet_amfi accepts it on the root
FS); the launchd job's keys all occur in real cached daemons except StandardOutPath (no cached
daemon uses it - if the agent's lines never show in the USB log, that is why); syslogd is disabled
on iOS 16, so the os_log helper should talk to diagnosticd (com.apple.os_trace_relay); stock tools
the agent can /run: /bin/ps, /bin/df, /usr/sbin/ioreg, nvram, hidutil, vm_stat, zprint, footprint,
spindump, sysdiagnose.

## >>> KUNZITE HANDOFF (2026-10-20 night) - READ THIS FIRST <<<

**State:** POCO M8 5G (`kunzite`, SM6475 parrot, Gunyah hypervisor) boots this repo's xnu to the
**iOS 16.7.16 lock screen in < 2 min on all 8 cores**, root = iPhone X HFS+ image flashed into
`userdata` (LU0 = sda). Clock runs (date = epoch + uptime: no RTC yet). **Buttons work (0907,
boot #18) and TOUCH WORKS (0908, boot #21): unlock, home screen, Settings/Messages/Photos open.**
No GPU (CPU render, some redraw artifacts), black wallpaper; the clock is red until the first power
press, then white (sRGB gamut, boot #19, changed neither). Branch `kunzite`, forked from
MakrSas/ios-topaz @ 590f001, merged with upstream d8c82d9 (0139, boot #22, kept); our patches are
0901-0908 (see below). Chronological log below.

**Standard boot** (phone in the BOOTLOADER fastboot = Power+VolDown; recovery's "Enter fastboot" is
fastbootd, which rejects `boot` -> `fastboot reboot bootloader`):
`fastboot boot shim/out/shim-boot-23.img` = kernel artifact d5f0c370 (kunzite = upstream 0139 +
0901-0909) + `shim/out/kunzite.adt`, cmdline below + `sweetusb=2` (USB Ethernet + live log: run
`tools/kunzite-usbnet-mac.sh` on the Mac)
(`tools/mkadt.py OUT --highmem`: RAM 0x900000000 + 6 GiB, vtimer INTID 27) + cmdline
`debug=0xc serial=3 cpus=8 keepsyms=1 -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1
ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1
sweetfb_noconsole=1 sweetfb_hz=30 sweetfb_stayon=1 ufslogsecs=1` (keys and touch on by default;
`sweetkeys=0` / `sweettouch=0` switch them off). Build: `python3 tools/mkpayload.py xnu/out/xnu-kernel/kernel.development.sweet shim/out/kunzite.adt
shim/out/payload.bin --cmdline '...'` then `shim/build-mac.sh out/payload.bin` (payload path is
relative to shim/; macOS build: brew aarch64-elf-gcc + temp/ios-m8/tools/mkbootimg.py).

**Must-keep switches and why:** `sweetfb_stayon=1` (display-off = topaz DCS via the real parrot
DSI0 -> white screen + SoC reset, boot #15; side effect: power-to-lock leaves the last frame on the panel, like an always-on display,
and the next press wakes iOS and redraws it - real screen-off needs a safe parrot DSI path); vtimer INTID 27 (the DT's PPI14/30 never delivers to
EL1, boots #10/#11); never touch CNTP_* at EL1 (Gunyah traps it: undefined instruction).

**Debug loop:** panic on screen -> `nm -n` / `xcrun llvm-objdump -d --start-address=` against the CI
artifact; kernel sources with all patches = the artifact's buildtree (temp/ios-m8/buildtree/xnu/).
User-space addresses: `ipsw dyld a2s <root image>/System/Library/Caches/com.apple.dyld/
dyld_shared_cache_arm64 <addr - slide>`. Persistent log (last 4 MiB of userdata): boot
`shim/out/shim-boot-logview2.img` (shim reads it from UFS, noise filtered, 3 pages; recovery adb is
unauthorized now); input-only view (SWEET KEY/HID/TOUCH lines): shim/out/shim-boot-loginput.img. Patches: edit a pristine copy, difflib -> xnu/patches/NNNN, `patch -p1 --dry-run`.

**Root image:** `tools/mkiosroot-mac.sh IPSW OUT.img` -> `sudo tools/fix-owners-mac.sh OUT.img
owners.json` (REAL owners: dmgs attached with -owners on; docker chown is a no-op) ->
`tools/img2simg.py` (FILL for zeros) -> `tools/verify-simg.py` -> `fastboot flash userdata X.simg`
WITHOUT -S (ABL max-download 353 MiB; fastboot 37 needs sparse for big images). Current image:
temp/ios-m8/ios16-root-20H392.{img,simg}, owners: temp/ios-m8/rootbuild.6iw7ol/owners-real.json.

**Our patches (0901-0908 since the 0139 merge; were 0127-0134 - older notes below use those
numbers):** 0901 ARMv8.2 back (inverse of 0081), 0902 sweet topology/boot CPU (inverse of
0080/0082), 0903 skip CNTP mask (Gunyah), 0904 parrot EPSS read-only + static battery, 0905 parrot
DSI0 + scanout maps off (regenerated over upstream 0138), 0906 UFS wake (SSU Active to W-LUN 0xD0:
ABL leaves UFS in PowerDown), 0907 keys on SPMI v7, 0908 GT9916K touch on SPI. Upstream topaz keeps
01xx; ours stay 09xx so CI's glob order applies them last. CI (blacktop build.sh) SKIPS any patch
that does not apply and still builds: after every merge run `tools/check-patch-series.sh
temp/ios-m8/series-check/xnu-pristine temp/ios-m8/series-check/dxb <scratch-tree>` (only the four
blacktop patches for other macOS versions may be skipped).

**M5 buttons - DONE (boot #18), reference:** port the SPMI read path to PMIC arbiter **v7** (Linux 6.6
spmi-pmic-arb.c, copy in temp/ios-m8/): bases from the live FDT - cnfg 0xc42d000, core 0xc400000
(VERSION at +0, expect >= 0x70000000), chnls 0xc500000, obsrvr 0xc440000, intr 0xc4c0000, ee 0.
APID map: core + 0x2000 + 4*apid, ppid = (val >> 8) & 0xFFF, entry 0 = unused; owner = cnfg + 4*apid
(v7; v5 was cnfg + 0x700 + 4n), count = core+4 & 0x7ff (<= 1024). Observer channel:
obsrvr + 0x8000*ee + 0x20*apid; CMD +0x00 = (1 << 27) | ((addr & 0xff) << 4) | (bytes-1),
poll STATUS +0x08 bit0, RDATA0 +0x18. ppid = (sid << 8) | (addr >> 8). Keys: power = SID0 0x1310
bit7, vol-down (resin) = SID0 0x1310 bit6 (PMK8350 pon_hlos@1300), vol-up = SID1 0x8810 bit0
(PM6450 GPIO1, pull-up, active LOW). **Patch 0133 (sweet_keys.c), proven on boot #18**: all
three keys reach iOS (volume HUD, power locks/wakes). Log lines `SWEET KEYS: arb=0x7... pon-apid=..
vol+-apid=..`, `SWEET KEYS: key=N down=D` (0 power, 1 vol+, 2 vol-), `SWEET HID: key 0x.. (N
clients)`; keys-only log viewer: `SHIM_EXTRA=-DLV_ONLY_SWEET shim/build-mac.sh` ->
shim/out/shim-boot-logkeys.img. sweet_spmi.c (battery path, SM7150 addresses) stays unused (static
battery). **NEXT:** PMK8350 RTC for the date; touch = Goodix GT9916K (brl-d) on SPI QUP1 SE9 0xa8c000, IRQ GPIO65, reset 131, IOVDD 38,
firmware goodix_3rd_firmware.bin + goodix_3rd_cfg_group.bin (from the LOS vendor partition).

**Upstream topaz merge (2026-10-20 night; booted as #22 and KEPT, kunzite fast-forwarded):** all of
upstream through 0139 (d8c82d9): 0127 sweetbusy diagnostic, 0128 iokit-quiesce, 0129/0130 WLAN
(inert: TopazModem runs only with boot-arg modem=1), 0131-0137/0139 SweetSurface (IOSurface
emulation: client memory/wrapped surfaces, values/attachments CoreAnimation's GL renderer reads -
upstream: "icons were blank under the GL renderer"), 0138 empty IOAcceleratorES (no 30 s wait at
every backboardd start). mkadt: sRGB was already ours; upstream's `/arm-io/wlan` node is opt-in
here (`--wlan`), default ADT unchanged. Boot #22 result below.

**Gotchas:** the fork's origin is mkukarin01/ios-topaz-experiments (user pushes + runs CI; artifacts
need GitHub auth -> user downloads to xnu/out/xnu-kernel/). Boots that must not write userdata need
`sweetlog=0` (the log flushes into userdata's last 4 MiB). LU1-LU4 are power-on write-protected by
ABL. Android no longer boots (userdata is HFS): restore with `fastboot -w` or recovery format.

## >>> KUNZITE PORT (2026-10-19, read this first) <<<

Port #3: **POCO M8 5G** (`kunzite`, Qualcomm SM6475 `parrot`, = Redmi Note 15 5G, model
25118PC98G). Base = MakrSas/ios-topaz @ 590f001 on branch `kunzite` of the user's fork
mkukarin01/ios-topaz-experiments. Full tagged-fact dossier: `../kunzite-hw-dossier.md`
(user workspace); live phone artifacts in `../../temp/ios-m8/phone-facts/` (outside git).

**State: first code delta committed, NOT yet CI-built with the kunzite patches, NOT yet
booted.** M0 done: the fork's `xnu` workflow ran green on unmodified 590f001
(run 39298601870). [obs]

Why kunzite should be EASIER than topaz (all from the live FDT / user observation):
- All 8 cores have the 16K granule: 4x A55 MPIDR 0x000-0x300 + 4x A78 0x400-0x700,
  **Aff1 = core index, Aff0 = 0 - exactly sweet's encoding**. No silver parking, no gold
  handoff, no LSE emulation (A78/A55 = ARMv8.2 with LSE+RCpc+PAN). [live]
- `fastboot boot` RAM-boot WORKS on the 2025 HyperOS ABL (user booted the LOS boot.img
  from RAM; phone survived reboot; nothing flashed). [obs]
- 12 GB RAM (spec sheet says 8): clean 6 GiB bank at 0x900000000 for `--highmem`. [live]

This delta (first kunzite commit):
- `tools/mkadt.py`: kunzite constants — GICD 0x17200000/GICR 0x17260000, splash FB
  0xb8000000+0x2b00000, UFS 0x1d84000, **VTIMER INTID 30** (parrot uses STANDARD PPIs
  13/14, unlike topaz's remapped PPI 3 → 19!), 8 cpu nodes (reg = core<<8), default RAM
  window 0xa8000000+0x38000000 (between global_sync end 0xa7000000 and cpusys_vm
  0xe0600000), `--highmem` = 0x900000000+0x180000000, panel 1080x2392.
- `shim/`: GICD/GICR bases, VTIMER 30 / PTIMER 29, IMEM restart reason 0x146aa65c,
  FB 0xb8000000 (console.h + mmu.h), UFS 0x1d84000 (ufs.c), **no pshold write** (parrot
  has none; khaje's 0x440b000 is unmapped here - removed as a hazard), and the GIC prep
  (test_gic + gold_gicr_prepare) now runs on the DIRECT boot path too (topaz proved xnu
  hangs after initCPU without redistributor wake + Group-1 SGIs/PPIs).
- `xnu/patches/0127-kunzite-armv8.2-restore.patch`: inverse of 0081's downgrade hunks
  (flags back to `-march=armv8.2-a+crypto`, commpage CASA, PAN on; 0081's emulation
  stays as dead code).
- `xnu/patches/0128-kunzite-cpu-topology-boot.patch`: inverse of 0080's SweetCPU hunk +
  all of 0082 (sweet topology check, boot MPIDR 0, startCPU guard). Generated
  programmatically from 0080/0081/0082 - context is byte-exact, hunk counts verified.
  0080's `sweethw=1` gating of the unported SM7150 key/touch/USB drivers stays.

Risks / unverified (GUESSES - first boot decides):
- UFS: 4096-byte logical blocks on ALL LUNs; GPT header understates entry count
  (sda claims 32, has 49; userdata = sda21). Kernel UFS driver untested here: keep the
  GPT fence, compare GPT before/after (baseline: temp/ios-m8/phone-facts/gpt-sd?-*.bin).
- What ABL leaves lit in the splash region 0xb8000000 at `fastboot boot` time is
  unverified (topaz's ABL lit its splash; assume yes until seen).
- Boot A55 core clock at handoff unknown; no EPSS boost on the direct path yet (domains
  0x17d91000 A55 / 0x17d92000 A78; add if first boots are slow).
- 0127/0128 apply AFTER the series reaches 0126; line numbers are the inverted
  originals - check the CI log applies them cleanly.

**BOOT #1 (2026-10-19 ~23:40, diagnostics shim, no payload): partial success + one finding.**
`fastboot boot` of our v4 image: accepted, jumped (OKAY 0.29s). Console painted (red), then
blue PANIC: `STAGE gic config`, `FAR 0x17270400` = GICR+0x10400 = SGI-frame IPRIORITYR,
ESR 0x96000010 (data abort, current EL), MPIDR 0x81000000 = U+MT, Aff all 0 → booted on CPU0.
Decoded: GICD 0x17200000 answers; redistributor RD frame works (TYPER match + WAKER wake);
the SGI-frame priority access aborts in this ABL/pKVM handoff state. So the topaz GIC prep
does NOT transfer to kunzite — removed from the direct path (sweet needs none; xnu's own GIC
init is the real test). Live FDT confirms redistributor-stride 0x20000, 1 RD region, cpus
reg 0x000-0x700 (2-cell) — frame math was right; this is an access-permission effect, not
addressing. Phone recovered (PSCI reset) and booted LOS. CI on kunzite: run 39302653336
GREEN (0127/0128 apply + build).

**BOOT #2 (2026-10-19 ~23:50, shim 27b02c3, no GIC prep, no payload): M2 ACHIEVED.**
Full diagnostics screen, clean run, phone auto-reset afterwards:
- `MMFR0 tg16=1` — 16K granule CONFIRMED on hardware; boot core = Cortex-A55 r2p0
  (MIDR 0x412FD050), MPIDR 0x81000000 (U+MT, Aff all 0) = CPU0.
- ABL PASSES the v4 boot ramdisk (INITRD 0xb6d6e000..0xb7ffef78, ~19 MB = our gzip
  cpio) — kunzite's ABL does NOT drop it, unlike topaz's.
- `LOADER no SWEETXNU payload` — payload scanner OK.
- UFS host @0x1d84000 ALIVE: UFSHCI **3.0.0**, CAP=0x1587031F (32 slots, 64-bit
  addressing), HCS=0x10F (DP=1: Samsung device present, queues ready). ABL's UTRP
  was at 0xFE934000, ours at 0xA8016000 → the shim image loads at ~0xA8000000, so the
  default mkadt window (PHYS_BASE 0xa8000000) overlaps the live image: FIRST PAYLOAD
  BOOT MUST USE `--highmem` (0x900000000).
- ALL commands fail: TUR/CAP/READ rc=4 ocs=2 — Linux OCS 0x2 = INVALID_PRDT_ATTR:
  our UTRP/PRDT is rejected by the v3.0 controller. Root-cause in shim/ufs.c (suspects:
  PRDT/UTRP format deltas for UFSHCI 3.0, 4096-byte logical blocks, UTRLBA swap
  procedure while the queue runs).

**BOOT #3 (2026-10-20 ~00:00, ufs.c instrumented): the UFS "failure" decoded — parked.**
Every command on every LUN 0-5: OCS=0 (controller SUCCESS), proper RESPONSE UPIU, but
response code resp[6]=0x01 = **UPIU "Fail"** with SCSI status 2 and EMPTY sense. That is
a device-side task-layer rejection (likely stale ABL task/nexus state after our list
takeover — khaje/SM7150 handoffs tolerated it, this one does not), not a descriptor bug.
The shim probe is diagnostics only: **parked**. The real UFS path is the kernel's
sweet_ufs.c (own host reset + init) — it gets its test in the payload boot.
Also done: `research/ipsw-x/d22ap.adt` extracted (remotezip + `ipsw img4 im4p extract`,
186 KB); `tools/mkadt.py` now builds the full tree with /product (no warning):
`shim/out/kunzite.adt` (--highmem), `kunzite-default.adt`, `kunzite-qemu.adt`.

**BOOT #4 (2026-10-20 ~00:20, first XNU payload: kernel from CI run 39302653336 +
kunzite.adt --highmem, cpus=1): XNU RUNS — killed by a QHEE timer trap.**
Shim found the payload; XNU booted with console up and early init running, then:
`panic(cpu 0): Undefined kernel instruction: pc=0xfffffe00074a2348 instr=0xd51be228 @sleh.c:1029`.
Symbolized against the CI kernel (`nm`): faulting pc = **`_fiq_context_init+0x14`**;
instr = `MSR CNTP_CTL_EL0, X8` — the *precautionary physical-timer mask* in
`_enable_virtual_timer()` (osfmk/arm64/machine_routines.c:2277), the ONLY CNTP write in
the tree. Cause: the EL2 monitor resident from XBL (QHEE) traps EL1 CNTP_CTL accesses
(CNTHCTL_EL2.EL1PCEN=0) on this SoC; SM7150/SM6225 allowed the write, so sweet/topaz
never saw it. Fix: **patch 0129** (under SWEET skip the mask; the kernel uses the
virtual timer — GIC PPI via ADT `virtual-timer-intid`), dry-run verified against the CI
buildtree. Open: whether `pe_init_fiq()` (GICR SGI-frame writes from EL1!) ran before
the panic — check the boot photos for the `SWEET GIC: virtual timer INTID 30` kprintf.

**BOOT #5 (2026-10-20 ~00:30, kernel with 0129): 0129 works — XNU reached IOKit.**
Console scrolled through memory init, AMFI/security policy, platform expert,
AppleKeyStore, IOWatchdog, **IOSurface (IOCoreSurfaceRoot)**, HID buttons + touch
service registration — then `Kernel data abort` at `pc=0xfffffe00074fdc20`
(`_sweet_cpufreq_max+0x88`), `far=VA+0x110` = the **LUT offset**: the kernel-side
topaz cpufreq boost (`bsd/dev/sweet_cpufreq.c`) maps khaje's `0x0f523000` EPSS domain
and faults on the first LUT read. Caller: `SweetBattery::start` (topaz battery, which
would next touch topaz I2C1/SPMI gauge MMIO). Fix: **patch 0130** (commit 5aaaed1) —
kunzite EPSS domains 0x17d91000/0x17d92000, READ-ONLY (PERF_STATE offset unverified on
parrot), SweetBattery static fallback by default (`sweetbattery=1` to experiment).
pe_init_fiq evidently ran (HID/IOSurface registration comes after it): **the GICR
SGI-frame writes work from EL1 inside xnu** — boot #1's shim abort was a shim-context
effect, not a general block.

**BOOT #6 (2026-10-20 ~00:55, kernel with 0129+0130): through CPU init into IOKit display.**
EPSS read-only LUTs printed: **A55 best=1804 MHz, A78 best=2400 MHz** (LUT offsets read-
validated). Battery static fallback line clean. **SWEET CPU: cpu=0 mpidr=0x0 came alive;
SGI1 OK; 1/1 CPUs initialized** — MAC Framework + buffer headers up, then a kernel data
abort at `AppleCLCD::start+0x2f4`: reading DSI0 ctrl at `0x05e94000+4` — sweet's SM7150
display MMIO (VIG0 0x5e05000, CTL0 0x5e02000, INTF1 0x5e6b000 in the same function) that
topaz never retargeted. khaje's fabric tolerated the read; kunzite's aborts. Fix: patch
**0131** — parrot DSI0 ctrl 0xae94000 (live FDT), VIG0/CTL0/INTF1 NULL (all uses guarded;
direct scanout off until the parrot SDE catalog is mined; DSI writes no-op without the
<4GiB DSI DMA buffer, which the highmem-only window cannot provide — remember this for
the direct-scanout milestone: it needs a lowmem window in the ADT).

**BOOTS #7-#12 (2026-10-20 00:55-01:30): M3 ACHIEVED — xnu boots to the root-mount panic.**
- #7 (+0131): DSI0 read works (ctrl 0x1f7), AppleCLCD on the splash 0xb8000000, full VM
  bootstrap on the 12G bank, then a HANG after gPanicBase lines (no panic, no progress).
- Diagnosis in the shim (#8-#10): a vtimer IRQ window (GICD/GICR subset only, no
  IPRIORITYR - that aborts, boot #1). **CNTP_* at EL1 is UNDEFINED under the EL2
  monitor — third strike after boots #4/#8; never touch it from EL1.**
- #10 verdict: CNTV asserts (tctl ISTATUS), pend bit 27 lit, bit 30 enabled -> nothing
  delivered (hppi/last 3FF). **The DT's virt=PPI14/INTID30 claim describes the EL2 view;
  for EL1 the vtimer lands on INTID 27.** #11 with bit 27: 30/30 IRQs taken (hppi/last
  0x1B). Fix: mkadt VTIMER_INTID 30 -> 27 (commit 8f2368d; xnu reads it from the ADT).
- #12 (payload + new ADT): the hang is GONE. AMFI/Sandbox/HFS/mcache/dlil/ifnet all
  init (threads that were sleeping on the timer now wake), SWEET UFS runs its kernel-side
  init and reports the device refusing commands (`upiu 0x21 response 0x1 status 0x2`,
  same UPIU-Fail class as the shim probe), then the expected end state:
  **`panic IOFindBSDRoot: SWEET UFS root ufsroot0 unavailable; 5`** — the kernel is
  complete and waiting for an iOS root image.

OPEN, in order:
1. iOS root image: Linux (VM or docker): apfs-fuse + hfsprogs + tools/mkiosroot.sh +
   sweet-root-tweaks.py from the iPhone10,3 20H392 IPSW; then the userdata flash
   (DESTRUCTIVE: GPT fence + fresh dumps + explicit user go).
2. UFS: even the kernel driver's commands fail (UPIU Fail, empty sense). Read
   bsd/dev/sweet_ufs.c's init (does it HCE-reset?), check LUN enumeration vs the
   shim probe's all-LUN Fail; the 4096-byte logical blocks and the GPT-fence (0088)
   apply the moment writes work.
3. Quick wins after root: cpus=8 (homogeneous PSCI path), console.c W/H -> 2392
   (cosmetic), sweetfb_hz tuning; then touch (Goodix GT9916 over SPI SE9 0xa8c000)
   and the display-quality milestones (need parrot SDE catalog + a lowmem ADT window
   for <4GiB DMA).

**OVERNIGHT (2026-10-20 ~03:00-03:40, autonomous): M4 PREP COMPLETE — iOS root image BUILT on macOS.**
- IPSW downloaded (6.17 GB, temp/ios-m8/ipsw/). tools/mkiosroot-mac.sh (new, commit f7776bc + fix):
  hdiutil replaces apfs-fuse (RO mounts of OS/SystemOS/AppOS dmgs: 087-86698-044/087-86593-047/
  087-86715-047, d22ap Erase identity), 321,213-entry ownership manifest captured and replayed as
  root inside docker (python:3-slim; 189 stragglers: hardlink dupes/suid refusals - tolerated)
  together with sweet-root-tweaks.py (launchd 7 PanicOnCrash + CommCenter removed, containers,
  VirtualAudio disabled, StartRecorderServer off, all prefs seeded; the mobileactivationd data ark
  SKIPPED - no container pre-first-boot; Hactivation via the ADT covers it).
- **temp/ios-m8/ios16-root-20H392.img** (16 GiB UDRW bare HFS+J, sha256
  8a53e27099ea1163743941fc27365c28b6a2aee057e3f63fe704f657f9d0049e) + **ios16-root-20H392.simg**
  (12.35 GB sparse, 204 chunks). Journal left clean via a mount/unmount cycle (fsck_hfs cannot
  run on plain files on macOS).
- FLASH PROCEDURE (DESTRUCTIVE, waits for the user + a working kernel UFS):
  `fastboot -S 512M flash userdata ios16-root-20H392.simg` (~25 min), fresh GPT dumps before/
  after, do not unplug, expect ABL fastboot to hang afterwards (re-enter by hand).
- Parallel: UFS root-cause workflow (sweet_ufs.c anatomy vs Linux ufshcd vs spec) running.

**OVERNIGHT part 2: UFS root-cause workflow + experiment matrix built and staged.**
Workflow wf_0fae6dbf-4b9 (4 agents) verdict, key corrections to earlier conclusions:
- The shim's "empty sense" was a DECODE ARTIFACT: the sense lives at resp[32:33] (BE16
  length) + data from 34 (key 36, ASC/ASCQ 46/47); the old print read the reserved 18-31.
- Doorbell-clear polarity bug: UTRLCLR is write-1-to-clear (shim + kernel both wrote ~1).
- Ranked cause: missing post-handoff device handshake (NOP OUT + fDeviceInit + descriptor
  reads) in BOTH contexts (ufs_take_over_host never HCE-resets); one-line alternative:
  REG_UFS_CFG1 (0x1d84000+0xDC) BIT(31) UTP_SCSI_CHECK_DIS may synthesize the failures.
- Escalation (ufsfull=1): HCE 0->1 + UIC DME_LINKSTARTUP x2, then the handshake.
IMPLEMENTED (commit f376828, image at shim/out/shim-boot.img, NOT yet booted - the
phone froze on boot #12's panic screen overnight): shim ufs_matrix() runs the whole
ladder (CFG1 probe+fix, TUR with correct sense, NOP OUT, device/LUN descriptors raw
dump, fDeviceInit set/poll, TUR+READ verdict) and HOLDS THE SCREEN (no reset).

MORNING (in order):
1. Hold Power+VolDown to get the phone to fastboot (it is frozen on the xnu panic).
2. `fastboot boot shim/out/shim-boot.img` -> the matrix screen prints and holds.
3. Photograph it; the verdict block (TUR/TUR2/READ r/s + sense + NOP + QDEV) picks the
   fix: CFG1 bit31 alone (one-line kernel fix), handshake (kernel patch 0132), or
   escalation (ufsfull prototype).
4. Then the M4 flash decision: `fastboot -S 512M flash userdata ios16-root-20H392.simg`
   (image ready; DESTRUCTIVE - only after UFS reads work and with explicit go).

**UFS MATRIX v1-v5 (2026-10-20 morning, shim-side, no CI): the protocol is alive; the LUs refuse.**
- v1: correct sense offsets revealed the real verdict: **04/02 "Logical Unit Not Ready,
  initializing command required"** on every command to every LUN (the earlier "UPIU Fail
  with empty sense" was our decode artifact). NOP OUT->NOP IN success. Queries failed 0xFE.
- v2: (query function byte at offset 4 - wrong). v3: **query_func is UPIU byte 5** (QEMU/
  Linux UtpUpiuHeader: iid_cmd_set_type=4, query_func=5) -> **READ_DESC device descriptor
  SUCCESS** (raw bytes incl. the Samsung 0x01CE marker at resp+56); SET_FLAG fDeviceInit
  -> 0xFF GENERAL_FAILURE; READ_FLAG fDeviceInit -> clear (val 0).
- v4: START STOP UNIT also rejected (04/02 class).
- v5: config descriptor read (idn 0x10) -> **0xFD INVALID_IDN**; INQUIRY, REPORT LUNS
  (W-LUN 0x81), TUR on all LUNs: all 04/02. Device descriptor raw (resp[32..63]):
  `00 00 00 59 04 06 00 00 05 01 00 01 00 04 01 00 25 10 20 02 03 02 01 00 1A 16 CE 01 03 00 00 02`.
  CFG1 BIT31 set+stuck (no effect). Full UPIU field layout now proven against QEMU structs
  (header 12B: trans/flags/lun/tag/iid_cst/query_func/resp/scsi_st/ehs/dev_inf/DSL;
  query struct 12-31; data 32+).
- Analysis workflow wf_fff9a73a-a61 (descriptor parse + driver lore + spec reasoning)
  running for the v6 design.

**UFS SOLVED (matrix v6, 2026-10-20 ~14:30, commit bfaab83): ABL leaves the device in UFS-PowerDown.**
- `bCurrentPowerMode` (query READ_ATTR idn 0x02) = **0x33 PowerDown** at handoff; bBootLunEn = 2 (boot LU B).
  That is why every LU answered 02/04/02 "initializing command required", even INQUIRY.
  Linux never sees it: ufs-qcom resets the device at probe; we take over without a reset.
- Cure: START STOP UNIT to the **UFS Device W-LUN 0xD0** with POWER CONDITION Active
  (cdb `1B 00 00 00 10 00`, as Linux ufshcd_set_dev_pwr_mode) -> power mode 0x11 Active,
  then TUR, INQUIRY (`SAMSUNG KLUFG4M1JB-C0F1`), READ(10) on LUN 0 all succeed; LBA 1 = `EFI PART`.
- Earlier "oddities" were shim bugs: config descriptor IDN is 0x01 (v5 sent 0x10); v4's SSU went
  to LUN 0 with the START bit (wrong target, wrong field for UFS power).
- Device descriptor (cross-checked with Linux sysfs): UFS 2.2 (0x0220), Samsung 0x01CE, 6 LUs,
  bBootEnable 1, bInitPowerMode 1, bHighPriorityLUN 5. Config descriptor: 0xE6 bytes.
- LUN map (unit descriptors; block counts match the sdX sizes from LineageOS):
  LU0 = sda 0x07686000 x 4K = 474 GiB (super + **userdata**), not write-protected;
  LU1/LU2 = sdb/sdc 8 MiB (xbl_a/xbl_b, boot LU A/B), LU3 = sdd 8 MiB (cdt, ddr),
  LU4 = sde 2.5 GiB (boot, vendor_boot, modem, ...): **all power-on write-protected by ABL**
  until the next power cycle; LU5 = sdf 64 MiB (modemst1/2, fsg), not write-protected.
- Kernel fix: **patch 0132** (sweet_ufs.c): SSU Active to W-LUN 0xD0 right after
  ufs_take_over_host(), before the first TUR; UTRLCLR timeout write fixed (write-1-to-clear,
  was ~1u); failure prints now include the LUN and sense key/ASC/ASCQ. Dry-run applied clean.

**BOOT #13 (2026-10-20 ~14:50, kernel with 0132): the kernel reads the UFS.**
`SWEET UFS: device power mode Active (SSU to W-LUN 0xD0): ok`, GPT on LUN 0 read with valid CRCs,
`"userdata" on LUN 0: first block 0x305b28, 473092 MiB, read-write` (matches the GPT dump: 3,169,064).
NOTE: with ufsrw=1 the persistent log then writes the last 4 MiB + 4 KiB of userdata every 10 s
(sweet_log_start) - boots that must not touch userdata need `sweetlog=0` (or ufsrw=0).
Then a kernel data abort in `sweet_keys_init+0xac`: topaz's PM6125 SPMI key driver maps topaz
addresses (core 0x01c40000, obs 0x03e00000, cnfg 0x01c0a000) that do not exist on parrot; the
topaz FT5452 touch driver (sweet_touch_init, right before it) is also wrong hardware for kunzite
(Goodix GT9916K on SPI SE9). Both have boot-arg switches: boot #14 adds `sweetkeys=0 sweettouch=0`
(image shim/out/shim-boot-14.img, no CI needed). Porting the keys to kunzite (PMK8350 PON + SPMI
GPIO, real SPMI arbiter base from the live FDT) is M5 work.
User (2026-10-20): LineageOS holds no user data and may be wiped -> M4 flash is next.

**M4 flash, attempt 1 (user, 2026-10-20 ~15:00): failed before sending anything.** Two causes:
the runbook gave a path relative to the rtk-tablet root while the terminal was in
ios-topaz-experiments, and the overnight `tools/img2simg.py` was broken (chunk total_sz without
the 12-byte header, trailing raw run dropped, no seek past don't-care runs). Fixed and
round-trip-tested (5 shapes byte-identical); the bad 12 GB file is renamed
`*.simg.BROKEN-headers`. Attempt 2 (raw image) failed the same way: fastboot 37's copy_avb_footer reads a whole NON-sparse
image into memory to move its AVB footer and dies 'Failed reading from userdata' on 16 GiB; sparse
images skip that step. Also fixed: zero runs were DONT_CARE (device keeps the partition's old bytes,
corrupting zero ranges inside files) -> now FILL 0. Regenerated simg VERIFIED against the raw image
(205 chunks: 191 raw + 14 fill, every check libsparse applies, byte-identical). Flash:
`fastboot -S 512M flash userdata /Users/oxa1/Work/rtk-tablet/temp/ios-m8/ios16-root-20H392.simg`
(volume checked: HFSX 'HX' v5, journaled, cleanly unmounted, 16 GiB). Then boot #14:
`fastboot boot shim/out/shim-boot-14.img` (sweetkeys=0 sweettouch=0).

**M4 FLASH DONE (2026-10-20 ~15:30): userdata = iOS 16.7.16 root (HFSX, 16 GiB at its start).**
Attempt 3 with `-S 512M` was refused: kunzite ABL max-download-size = 370147328 (353 MiB). Without -S
fastboot sizes the pieces itself: `fastboot flash userdata .../temp/ios-m8/ios16-root-20H392.simg`
-> 37/37 OKAY in 321 s. ABL writes asynchronously ("Writing OKAY" in ms, the wait shows up on the
next transfer; the following `fastboot boot` took 67 s). LineageOS userdata is gone (user OK'd it:
no user data); Android needs `fastboot -w` / recovery format to boot again.
Boot #14 started right after: shim/out/shim-boot-14.img (kernel 0132, kunzite.adt --highmem,
cpus=1, sweetkeys=0 sweettouch=0, console on).

**BOOT #14 (2026-10-20 ~15:40, flashed root): M4 ACHIEVED - xnu mounted the iOS root from UFS.**
launchd (pid 1) ran libignition (`hello from launchd.1`, dylib_cache found
/System/Library/Caches/com.apple.dyld), then the kernel refused to map the shared cache:
`not in trust cache` (expected; lp-sip0 relaxes it) and **`owned by uid=501 instead of 0`** ->
launchd: `Library not loaded: /usr/lib/libSystem.B.dylib` -> panic `initproc failed to start`.
Cause: EVERY file in the macOS-built image is 501:20. The IPSW dmgs were attached without
`-owners on`, so the ownership manifest recorded the Mac user for all 321,213 paths, and the
docker replay could not have helped anyway (Docker Desktop bind mounts drop chown).
Fix: tools/capture-owners.py (real manifest, temp/ios-m8/rootbuild.6iw7ol/owners-real.json:
311,912 root, 9,270 mobile, _logd 272 ...; refuses a one-owner manifest),
tools/apply-owners.py (lchown + chmod, symlink-safe), tools/fix-owners-mac.sh (sudo: all
root:wheel, manifest, builder-made paths, tweaks again, checks), mkiosroot-mac.sh fixed
(-owners on, no docker). Then regenerate the simg, verify, reflash (5 min), boot #15.

**Ownership fixed (2026-10-20 ~15:20):** `sudo tools/fix-owners-mac.sh ...img .../owners-real.json`
-> owners applied 321,162, missing 47 (moved VirtualAudio plugin, excluded .file), errors 0;
checks: dyld cache 0:80, launchd 0:0, mobile 501:501, diagnostics 272:272, systembag.kb 0:0,
springboard.plist 501:501. simg regenerated, `tools/verify-simg.py` VERIFIED (205 chunks).
Reflash (no -S), then boot #15 with the same shim/out/shim-boot-14.img.

**BOOT #15 (2026-10-20 ~15:35, ownership-fixed root): THE APPLE LOGO - iOS draws on kunzite.**
The shared cache maps, launchd runs the system, **backboardd (pid 56) draws the Apple logo** through
the kernel IOSurface/AppleCLCD path onto the ABL splash framebuffer; **SpringBoard (pid 26) is up**.
The logo is then overwritten by the kernel console (console still on). bluetoothd/fairplayd.H2
relaunch loops = missing hardware (as on sweet/topaz). Stack sampler, symbolicated against the
image's shared cache (slide 0x2da14000, `ipsw dyld a2s`): SpringBoard t0 waits in
`_LSCopyServerStore` <- `___LAUNCH_SERVICES_WAS_ASKED_TO_PERFORM_BLOCKING_IO_ON_THE_MAIN_QUEUE__`
<- sync NSXPC to lsd: the first-boot LaunchServices database build (topaz's "3 minutes on the
logo"), slow on cpus=1. The database persists on the root (rootrw=1): one-time cost.
Next: boot #16 = same + `sweetfb_noconsole=1 sweetfb_hz=30` (shim/out/shim-boot-16.img) so the
GUI stays visible; then cpus=8 as a separate experiment (secondary-core GICR setup untested here).

**Boot #15 aftermath (2026-10-20 ~16:00): ~12 min in, a white screen, then the phone RESET** (into
Android -> recovery "Can't load Android System": expected, userdata is HFS now). The shim log viewer
(shim-boot-logview.img, commit cfad71f: reads SWEETLOG straight from UFS, 2x text pages) shows the
last ~200 console lines: sample 6, SpringBoard + backboardd t11 still in the LaunchServices wait,
then only the bluetoothd/fairplayd relaunch loop - no CRASH, no panic, no reboot message.
Analysis: our kernel CANNOT reset the SoC (PE_halt_restart is NULL, no PSCI SYSTEM_RESET anywhere):
a reboot request from iOS would panic on screen. So the reset came from below the kernel.
kunzite runs **Gunyah** (dmesg: gh-watchdog bark 20 s / pet 15 s, enabled by Linux's driver; boot
#12 held a panic overnight -> not armed at handoff). Suspect: the display path - the white screen came
first, and topaz's display-off (DCS 0x28 via DSI DMA, AppleCLCD::setPanelOn) now hits the REAL parrot
DSI0 (0131); it is guarded by fDsiBuf (<4 GiB alloc, likely NULL in the highmem-only window) but
unproven. Boot #16 (shim/out/shim-boot-16.img): + sweetfb_stayon=1 (topaz's switch: ignore display-off
requests), ufslogsecs=1 (lose <=1 s of log on a reset), sweetfb_noconsole=1 sweetfb_hz=30 (see the GUI).
Also for M5: the SPMI PMIC arbiter is at 0xc42d000 (dmesg), PON on PMK8350 pon_hlos@1300.

**BOOT #16 (2026-10-20 16:25): THE iOS LOCK SCREEN on kunzite.** shim-boot-16.img (kernel 0132, cpus=1,
sweetkeys=0 sweettouch=0 sweetfb_noconsole=1 sweetfb_hz=30 sweetfb_stayon=1 ufslogsecs=1).
Apple logo 16:25-~16:41 (first-boot LaunchServices database, continued from boot #15; persists on
the root), then SpringBoard's lock screen at ~16:41; clock updating (:18 -> :20), still alive past
the point where boot #15 reset. Black background, time/date drawn RED (not a channel swap: white
is invariant under channel order -> iOS-side colours; guess: iOS 16 wallpaper/PosterBoard fails
without a GPU, clock tint derived from it). Battery icon: SweetBattery static (100%, external).
Boot #15's white screen + reset was most likely the display-off path (auto-lock -> AppleCLCD::
setPanelOn(false) -> DSI DMA on the real parrot DSI0): KEEP sweetfb_stayon=1 until the DSI path is
ported (also needed anyway: no buttons/touch to wake the panel).
Next: boot #17 = same with cpus=8 (shim/out/shim-boot-17.img); then M5 input (SPMI keys via the
arbiter at 0xc42d000 / PMK8350 PON 0x1300, Goodix GT9916K touch on SPI SE9 0xa8c000).

**BOOT #17 (2026-10-20 17:58:22, shim-boot-17.img): cpus=8 WORKS - lock screen in under 2 minutes**
(user report before 18:00; boot #16 on one core: 16 min). All 8 cores (4x A55 + 4x A78) start through
sweet's PSCI path with 0128's topology; each core's pe_init_fiq sets up its own redistributor and
the INTID 27 vtimer - no shim-side GICR prep needed (topaz needed gold_gicr_prepare).
Standard boot args now: `debug=0xc serial=3 cpus=8 keepsyms=1 -noprogress rd=ufsroot0
unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1
launchd_unsecure_cache=1 wdt=-1 sweetkeys=0 sweettouch=0 sweetfb_noconsole=1 sweetfb_hz=30
sweetfb_stayon=1 ufslogsecs=1`. Log viewer v2 (whole ring, noise filtered): shim-boot-logview2.img.
Next: stability soak, then M5 input (keys via SPMI 0xc42d000 / PMK8350 PON 0x1300; Goodix GT9916K
touch on SPI SE9 0xa8c000), RTC from the PMIC, wallpaper/red-clock question.

**BOOT #18 (2026-10-20 18:31, shim-boot-18.img): BUTTONS WORK** - kernel from the fork tip (0133,
artifact sha256 283b12cb...), boot #17's cmdline minus `sweetkeys=0`. After a 20-minute lock-screen
soak on boot #17 with no issues. User report: volume up and down work; power works (wakes and
redraws the lock screen: the date/clock update on each press). With `sweetfb_stayon=1` the panel is
never switched off, so "locked" looks like an always-on display. A blue icon near the battery
pulses on power presses (unidentified, maybe colour-shifted by the P3/sRGB issue).
Next: boot #19 = same + ADT `artwork-display-gamut sRGB` (upstream topaz 3eacb48), for the black
wallpaper / red clock.

**TOUCH (2026-10-20 evening): patch 0134 compiled first try (artifact sha256 18eea468...); boot #20
(shim-boot-20.img = #18 cmdline minus sweettouch=0) RAM-booted 22:29.** Input log viewer:
shim/out/shim-boot-loginput.img (SWEET KEY/HID/TOU lines only). New bsd/dev/kunzite_touch.c: Goodix GT9916K (Berlin-D) on SPI QUP1 SE3 0xa8c000,
FIFO mode, polled every 8 ms; started from sweet_ufs.c instead of topaz's sweet_touch.c (SM6225
I2C). Facts: SPI pins GPIO10-13 func 1; TLMM pin regs at 0xf100000 + 0x1000*N; IRQ 65, reset 131,
IOVDD GPIO38; AVDD = PM6150L L7E 3.3 V via RPMh "ldoe7" (apps RSC DRV2 0x17a20000, TCS +0xd00, v3
layout, ACTIVE TCS 0-2, cmd-db 0x80860000); GCC vote 0x62008 bits 18-21,25, S3 RCG 0x283b0 (DFS);
Android log proves FIFO mode ("tx_fifo 16"). Protocol from Linux goodix_berlin_{core,spi}.c.
sweet_keys.c exports sweet_spmi_peek() so the driver logs L7E status before/after the vote.
Sources in temp/ios-m8/touch-research/src/. Boot-args: sweettouch (1), sweettouchdelay (45 s),
sweettouchpower (1; 0 = no RPMh vote), sweettouchdiv (4 -> 4.8 MHz), sweettouchlog (24).
Next: push -> CI (fix compile errors if any) -> boot #20 = #18 cmdline WITHOUT sweettouch=0;
read "SWEET TOUCH" lines with the log viewer (extend LV_ONLY_SWEET to "SWEET TOU").

**BOOT #20 result (log viewer, user transcription): touch chain works up to the IC, SPI opcode
wrong.** Clocks on (vote 0x4000a3c0 -> 0x423ca3c0, S3 DFS level 0 = XO/1), SE3 fw=0x102 proto=1
FIFO depth 16 x 32 (QUP 0x300a0000), 4.8 MHz. RSC DRV2 id 0x20700 = v2.7 layout, TCS=8 ncpt=16,
ldoe7 = VRM 0x42b00; ACTIVE TCS0 vote 3300 mV done (cmd-status 0x10101 x3). L7E status unreadable:
SID 4 0x46 has no entry in the apps APID table (PM6150L is on bus 0; SPMI peek -> ENOENT). Pins as
found: touch fully off (iovdd/reset low, SPI pins gpio). Device confirm read 00 00 00 00 00 45 52 4c
three times. Cause: M_CMD opcode 7 (SPI_TX_RX) = TX then RX, so reads returned mem[addr+16..] and
the 8 x AA write ran on with 13 zero bytes (0x10008-0x10014, clobbering the 'B' of the ROM PID
"BERLIN" at 0x10014: hence 00 'E' 'R' 'L'). Fix in 0134 (regenerated): opcode 3 = SPI_FULL_DUPLEX
(spi-msm-geni.c). Also: L7E log says "unreadable", the 250 ms stale-finger release is now opt-in
(sweettouchstale=ms) so long presses survive an IC that stops reporting a still finger.

**BOOT #21 (2026-10-20 23:05, shim-boot-21.img, kernel e3a9fd81..., kunzite 6d8fcd5): TOUCH
WORKS.** #20's cmdline (touch on), 0134 with SPI opcode 3. User: unlocked the lock screen, opened
Settings, Messages and Photos - all fine. Power button locks "on the 2nd press" (guess: iOS had
already slept the display, stayon kept the frame visible, press 1 = wake). Some redraw artifacts
when switching between apps/spaces (CPU rendering). No log read this time.
**BOOT #22 (2026-10-20 23:33, shim-boot-22.img, kernel 4c641f3c..., branch kunzite-topaz-0139 @
de778a9 = upstream 0139 merge + 0901-0908):** same cmdline and ADT as #21. User: boot time no
visible difference (not timed), wallpaper still black, icons fine (were fine on #21 too), switching
between apps/spaces SMOOTHER (the second swipe leaves no artifacts), power still locks on the 2nd
press, volume fine. One regression-ish note: Settings > General > About opened immediately on #21,
took a while on #22. Decision: keep the merge; `kunzite` fast-forwarded to it.

**BOOT #23 (2026-10-21 00:15, shim-boot-23.img = #22 + `sweetusb=2`, kernel d5f0c370...): USB
ETHERNET + LIVE LOG WORK, first try.** The Mac sees "Kunzite iOS (USB Ethernet)" as en7 34 s after
`fastboot boot` (link active); after `tools/kunzite-usbnet-mac.sh` (sudo once: network service set
to manual 192.168.7.1/24, persistent) `ping 192.168.7.2` = 2 ms, 0 % loss, and the console streams
into temp/ios-m8/usblog/live-*.txt. Gap: the first 64 KiB were sent before the Mac had its address
and were lost (the log thread advances its position even when sending fails) - fix: replay on
request (0910). Seen live: touch confirm `aa x8`, fw BERLIN / 9916K, touch_data_addr 0x10308.

**USB ACCESS (2026-10-21, patch 0909):** sweet's USB device mode ported to parrot:
CDC-ECM Ethernet `ue0` 192.168.7.2/24, live console as UDP to 192.168.7.1:5140 (starts with the last
64 KiB). Changes vs sweet: GCC GDSC 0x49004 and clocks 0x49018/20/24/84/88 (gcc-parrot.c), femto PHY
not read (sweet read QUSB2 at 0x88e2000), device mode only if GHWPARAMS0 AWIDTH ((v >> 24) & 0xff)
== 64 (kernel RAM is above 4 GiB), no sweethw=1 needed (default read-only report; `sweetusb=2`
starts it). Same DWC3 base/QSCRATCH VBUS override (Xiaomi dwc3-msm-core.c); UFS proves device DMA
to high memory passes the SMMU untranslated. Mac: `tools/kunzite-usbnet-mac.sh` (finds the
interface by host MAC 02:00:5e:77:ee:01, sets its network service to manual 192.168.7.1 once with
sudo, then runs `tools/udp-log.py` -> temp/ios-m8/usblog/). Plan after it works: SSH into iOS
(ad-hoc signed dropbear + LaunchDaemon in the root image; sweet_amfi accepts any valid signature on
the root FS as a platform binary with its own entitlements, no sandbox), then an os_log streamer.

**AGENT (2026-10-21, agent/, not on the phone yet):** kunzite-agent.c - HTTP/1.0 debug service on
tcp/8022 (peers: 192.168.7.0/24 + loopback only), runs as root from launchd with stdout/stderr to
/dev/console (= the live USB log). Stock iOS has no shell, so the agent IS the remote side: ls/stat/
get/put(chunked too)/rm/mv/mkdir/ps/kill/run (spawn with streamed stdout+stderr, bounded t)/exit
(restart via KeepAlive). Build: `agent/build.sh` (arm64 iOS 16 + ldid -S entitlements.plist; also a
macOS binary for local tests). Install into the root image: `sudo tools/root-add-agent-mac.sh`
(copies to /usr/libexec/kunzite-agent, adds the launchd job to the launchd.plist cache + writes
/System/Library/LaunchDaemons/com.kunzite.agent.plist; idempotent), then img2simg + flash userdata.
Mac use: `curl -s 'http://192.168.7.2:8022/ls?p=/'`. Reviewed by a 3-lens adversarial workflow
(23 findings, several verified live: /run could wedge the agent forever on client disconnect, bare
query params crashed it, /kill pid wrapping could target launchd, chunked uploads were silently
dropped) - all fixed and re-verified on the Mac build: socket timeouts (AGENT_RCV/SND_TIMEOUT_S),
POSIX_SPAWN_CLOEXEC_DEFAULT + SIGPIPE reset in children, bounded reaping, range checks. NEXT:
install + flash (user runs sudo + fastboot), then /ping from the Mac; then os_log streaming (the
kernel console misses SpringBoard/os_log output - a /run helper reading the log stream).

**NEXT (candidates):** RTC/date (PMK8350 RTC + boot-time offset); real screen-off (a safe DCS path
on the parrot DSI0, then drop sweetfb_stayon); black wallpaper (upstream topaz: check whether its
lock screen has one; PosterBoard/wallpaper renderer logs); About-pane slowness on #22 (sweetbusy=N
from upstream 0127 shows who keeps IOKit busy); Wi-Fi: see ROADMAP FACTS (2026-10-22) - the WLAN is
the same modem-integrated family as topaz's, the earlier "not topaz's WCN3988" was wrong.

**BOOT #19 (2026-10-20 ~19:15, shim-boot-19.img, user-typed fastboot): sRGB gamut - no visible
change.** Same kernel as #18 (283b12cb; the later CI artifact 0da72d25 has no kernel source change),
ADT P3 -> sRGB. Wallpaper still black. Observed (also likely on #18): the clock/date are RED after
boot until the first power press; after a sleep/wake they are WHITE (photo: rtk-tablet
ios/photos/photo_2.jpeg). The date reads "Wednesday, December 31 4:13" = 1970-01-01 00:13 UTC in
iOS's default Cupertino time zone, i.e. epoch + 13 min uptime (confirms: no RTC). The pulsing blue
icon under the battery is the Control Center glyph (SF Symbol switch.2) - most likely SpringBoard's
"teachable moment" hint (swipe down from the top-right for Control Center), shown on wake; not a
fault. Volume presses gave no HUD this time - guess: iOS had gone to sleep (lock-screen idle
timeout) and does not render while asleep, but stayon keeps the stale frame visible; to check:
press power, then volume within a few seconds. sRGB stays in mkadt (harmless; upstream needs it for
Safari).

NEXT (in order):
1. Push kunzite, run the `xnu` workflow, fetch the artifact (0132).
2. Boot #13 (same payload cmdline, kunzite.adt --highmem): expect
   `SWEET UFS: device power mode Active ... ok`, then userdata found on LUN 0 (GPT), then a
   panic at the HFS root mount (userdata still holds LineageOS). Non-destructive.
3. M4 flash (DESTRUCTIVE, needs the user's explicit go: erases LineageOS userdata):
   `fastboot -S 512M flash userdata temp/ios-m8/ios16-root-20H392.simg`, then boot #14.

NEXT (older list, superseded):
1. Push 0130 (commit 5aaaed1), run the `xnu` workflow, fetch the artifact.
2. Boot #6 (same mkpayload cmdline). Watch for: `SWEET CPUFREQ: A55/A78 LUT best …`
   lines (first parrot EPSS readout), how far IOKit gets, and the eventual stop —
   expected the root-mount complaint (`rd=ufsroot0`, no image flashed) or a
   sweet_ufs.c report (the Samsung device "Fail" state gets its kernel-side test).
3. Then the iOS root image (Linux: apfs-fuse + hfsprogs + mkiosroot.sh +
   sweet-root-tweaks.py from the IPSW; flash userdata = destructive: GPT fence,
   fresh dumps, user confirms).
- Phone: LineageOS 24 (2026-09-16 unofficial) installed, `adb root` = "ADB only"
  enabled; the user runs fastboot/GitHub personally. Never `fastboot getvar` (sweet
  ABL lesson); do not unplug during flashes.

## Agent handoff / active work (2026-10-15, latest)

### >>> UPDATE 2026-10-18 (read first, then the 10-17 handoff below) <<<

**LIVE LOG over USB (0077, boot 0077s): no more recovery round-trips.** Boot an image with `sweetusb=2`, run `tools/sweet-usbnet.sh logs/live-NAME.txt` on the PC: it waits for the phone's CDC-ECM interface, sets 192.168.7.1/24 (sudo) and prints/saves the kernel console the phone streams every 100 ms as UDP to 192.168.7.1:5140 (starts with the last 64 KiB). `ping 192.168.7.2` checks the link. Current image: `shim/out/shim-boot-0077s.img` (kernel 0077, ADT sweet-0069hi, cpus=6, sweetfb_noconsole=1, sweetfb_hz=30, sweetusb=2). The recovery pull (`tools/sweet-log-pull.sh`) is still the way after a panic/hang before USB comes up.

**🎉 USB networking works (boot 0076s, 18:24 local): `ping 192.168.7.2` from the PC answers in ~1 ms, 0 % loss (200-packet burst, 1500-byte frames, 512/1024-byte frames).** 0075: CDC-ECM function (bulk 0x81/0x01, interrupt 0x82 NETWORK_CONNECTION; host MAC 02:00:5e:77:ee:01) + kernel ethernet ifnet `ue0` (legacy output, header strip like if_fake) with 192.168.7.2/24 set from the kernel (proto_plumb + SIOCAIFADDR on an in-kernel socket). 0076: received frames into one cluster mbuf (mbuf_allocpacket gave a first mbuf of length 0 -> panic "Failed mbuf validity check: len -14"). Host side (Linux cdc_ether, interface `enp0s20f0u3u1`): `nmcli dev set IF managed no; sudo ip addr add 192.168.7.1/24 dev IF; sudo ip link set IF up`. Boot image `shim/out/shim-boot-0076s.img` (`sweetusb=2`). iOS itself does not see ue0 yet (configd needs an IONetworkInterface in IOKit).

**USB device mode works (boot 0074s, 17:45 local): the host enumerates the phone at high speed as `1209:0001 ios-sweet "Sweet iOS (USB bring-up)"`.** 0072 read-only probe: ABL leaves the USB30_PRIM GDSC and all 7 USB clocks on, DWC3 v3.20a (GSNPSID 0x5533320a) in device mode (GCTL 0x00112000), halted (DCTL RUN_STOP 0, DSTS 0x00d3de04), event buffer still at ABL's 0xfdfc8000, QUSB2 PHY alive. 0073 (`sweetusb=2`): DCTL soft reset, own event buffer (polled, interrupt masked), ep0 DEPSTARTCFG/SETEPCONFIG/SETTRANSFRESOURCE, setup TRB, RUN_STOP; control-transfer state machine as Linux ep0.c; descriptors: vendor class, no data endpoints. It ran but no event came: **0074** forces the VBUS session in the Qualcomm wrapper (QSCRATCH 0xa6f8800: HS_PHY_CTRL |= UTMI_OTG_VBUS_VALID(20)|SW_SESSVLD_SEL(28), SS_PHY_CTRL |= LANE0_PWR_PRESENT(24), as Linux dwc3_qcom_vbus_override_enable) -> connect + enumeration. Image `shim/out/shim-boot-0074s.img`. Black Phone app: main thread waits in -[VMVoicemailManager fetchAccounts] (sync XPC; voicemail needs the modem).

**Navigation works (boot 0071s on v14, log `logs/boot-0071s.txt`):** swipe up from the bottom edge returns to the home screen; app switcher works. Two fixes: **0070** pid_hibernate (syscall 435 was nosys on the macOS config -> SIGSYS killed SpringBoard in -[SBToAppsWorkspaceTransaction _didComplete] when an app left the foreground; the only CONFIG_EMBEDDED-only entry in syscalls.master); **0071** touches starting within 48 px of an edge carry kIOHIDDigitizerEventFromEdgeTip (UIKit `_UIEventHIDTouchEdgeTypeForHIDEvent` reads the digitizer EventMask 0xb0007 bits 0x800/0x1000/0x2000/0x40000). Diagnosed, not fixed: Settings (Preferences) SIGTRAP in PodcastsUI func_1cdc47c58: force-unwrap of `URL.volumeTotalCapacity` (nil on our HFS+ root). Phone/Photos/Messages show black (MobilePhone launches, no crash) - next: stack samples via boot-arg `sweetsample=MobilePhone,MobileSlideShow,MobileSMS,SpringBoard` (image `shim/out/shim-boot-0071s.img`).

**🎉🎉 MILESTONE 2026-10-18 ~15:30 local: iOS 16.7 SPRINGBOARD HOME SCREEN ON THE PHONE** (boot `shim/out/shim-boot-0069s.img` = kernel 0069 + ADT `shim/out/sweet-0069hi.adt`, image **v14**; log `logs/boot-0069s-v14-HOMESCREEN.txt`). Home screen with a few apps, Settings opens, Control Center / lock screen / notification center work, touch + buttons work, 6 A55 cores (`cpus=6`), `sweetfb_noconsole=1 sweetfb_hz=30`. Known issue: cannot leave Settings (home gesture / AssistiveTouch home do nothing) - to investigate.
What it took today (in order): 0058 HID services (Apple IOHIDFamily-1915 user client), 0059 iPhone X identity (hw.product etc.), 0060 SPMI keys + GENI touch, 0061 1125x2436 presented display, 0062 RPMh L11C touch power + IOPMPowerSource battery, 0063 GENI FIFO packing (Goodix GT9896), 0064 PRIV_WORK_INTERVAL grant (SpringBoard "Failed to create work interval"), 0065 SMP via PSCI (6 A55), 0066 crash map regions, 0067 console mute without cons_ops write, 0068 IOSurface lookup of an already-mapped surface returns base -1 (backboardd snapshot crash), 0069 UFS log flush 10 s. Image v13: CommCenter removed from launchd.plist, BKNoWatchdogs. **Image v14** = v13 + Setup skipped (`/var/mobile/Library/Preferences/com.apple.purplebuddy.plist` SetupDone/SetupFinishedAllSteps true, SetupVersion 11, daemonizeNeedsToRun false; purplebuddy.notbackedup mini-buddy flags false; managedconfiguration.notbackedup LockdownActivationIndicatesCloudConfigurationAvailable false; springboard MCForceShowCloudConfigurationUI false) + mobileactivationd data ark seeded (`/private/var/containers/Data/System/D90C94C4-F94E-4FCF-8A28-A93BE3846D8E/Library/internal/data_ark.plist`, -ActivationState Activated etc.) + MediaToolbox D22/D221/Default SubServerConfiguration StartRecorderServer=false (mediaserverd crash loop: portrait-filter prewarm called NULL GL). **Activation:** ADT /chosen certificate-production-status 0, effective-production-status-ap 0, certificate-security-mode 1, effective-security-mode-sep 1 -> mobileactivationd's native Hactivation branch (Codex `codex/activation/report.md`; alternative 4-byte daemon patch documented there). Fallback image v13; v12 deleted.

**🎉 TOUCH WORKS (boot 0063u on image v13, 2026-10-18 ~13:15 local):** tapping (i) on Setup's Hello opens the device-info sheet; swipe-up is recognised (spinner, then back to Hello - under investigation). Chain: RPMh L11C vote (0062, `bsd/dev/sweet_rpmh.c`) -> GT9896 @0x5d on GENI SE7 (0063 fixed FIFO packing: register address went out byte-swapped) -> sweet_hid_touch -> SweetHIDTouch -> backboardd. Battery icon OK (0062 SweetBattery IOPMPowerSource, static 100 %). **Image v13** (= v12 + CommCenter jobs removed from launchd.plist + `BKNoWatchdogs`=true and `FBLaunchWatchdogExceptions{com.apple.purplebuddy:100}` in /var/mobile/Library/Preferences/com.apple.springboard.plist): Setup no longer killed by the scene-create watchdog (it blocked in CoreTelephony getSubscriptionInfo -> CommCenter, Codex `codex/setup/report.md`; that report also lists how to skip Setup: SetupDone/SetupFinishedAllSteps/SetupVersion=11 in com.apple.purplebuddy.plist + activation check). v9 deleted, fallback = v12. Input latency ~1 min with 1 CPU -> Codex task 11 = SMP via PSCI (`codex/smp/`).

**INPUT WORKS (boot 0060u on a freshly flashed v12, log `logs/boot-0060u.txt`):** power, volume up, volume down reach iOS (PMIC -> sweet_keys.c -> SweetHIDButtons -> backboardd). Power locks/unlocks (screen "off" shows the kernel console because the panel never really turns off). **Device identity fixed: iPhone X** (home indicator, Face-ID-style Setup). Earlier test (0059u, `sweethidtest=15` fake Home presses) advanced Setup from Hello to the language list.
- **0058** HID: `iokit/Kernel/arm/hid/`: IOHIDEventServiceUserClient + IOHIDEventServiceQueue + IOHIDEvent compiled UNCHANGED from Apple IOHIDFamily-1915.140.3 (= exact version of the iOS 16.7 IOHIDLib, clone in `research/iohid-1915`) over our minimal IOHIDEventService (SweetHID.cpp: SweetHIDButtons consumer page 0x0C power 0x30 / vol 0xE9/0xEA / menu 0x40; SweetHIDTouch digitizer collections). C hooks `sweet_hid_key(key, down)` (0 power, 1 vol+, 2 vol-, 3 home) and `sweet_hid_touch(finger, down, x, y)`. HID logs go to IOLog. Boot-arg `sweethidtest=N` = fake Home press every N s.
- **0059** identity: hw.machine/hw.model/hw.target/hw.product from the ADT (macOS-config kernel used to answer "iPad8,6"/"Redmi Note 10 Pro"). mkadt: root compatible `D22AP|iPhone10,3|xiaomi,sweet|...`, model iPhone10,3, target-type D22, `/product` copied from the real D22AP device tree (`research/ipsw-x/d22ap.adt`, extracted from the IPSW, NOT in git), `/buttons home-button-type 2`. Codex report `codex/mg/report.md`: MG ProductType = sysctl **hw.product** (unrecognized -> iPod), HWModelStr = hw.target, HomeButtonType = `/buttons/home-button-type`; disk cache `/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist` (keyed by build + UUID only -> stale iPod answers survived; reflashed clean v12 to clear it).
- **0060** drivers by Codex (`codex/input/hw-report.md`): `bsd/dev/sweet_keys.c` (SPMI arbiter v5 observer reads, PON 0x810 bits KPDPWR/RESIN SID0, PM6150L GPIO2 SID4 0xc110; works), `bsd/dev/sweet_touch.c` (GENI SE7 I2C @0xa84000, starts 45 s after boot). Touch result: SE7 fw ok (proto 3) but **0x5d and 0x38 both time out -> touch IC unpowered** (L11C LDO needs an RPMh vote). Codex task 8 = RPMh client (`codex/rpmh/`), task 7 = battery (`codex/battery/`).
- **0061** (CI 39117078444): red status bar "rdar:45025538" = UIKit "No status bar visual provider for screen" (360x800 pt is no real iPhone). AppleCLCD now presents **1125x2436** (iPhone X) and shows the centered 1080x2400; touch mapped the same; ADT canvas 1125x2436. `sweetfb_native=1` reverts.
- Images: `shim/out/shim-boot-0060b.img` (0060 + /buttons ADT `sweet-0060hi.adt`). Next: 0061 image with ADT from current mkadt.

### >>> HANDOFF 2026-10-17 late (read this first) <<<

**STATE: iOS 16.7 Setup.app "Hello" screen is ON THE PHONE** (boot `shim/out/shim-boot-0057u.img` on image v12). Photo `/run/media/dipa/android/ios-sweet-work/MILESTONE-setup-hello-2026-10-17.jpg`. SpringBoard + backboardd + Setup run; nothing crashes in the GUI path. Remaining crash/relaunch loops: installcoordinationd (should be fixed by 0057, unverified), mediaserverd (VirtualAudio removed in v12, unverified), mobileactivationd (~100/boot, not analysed), bluetoothd + fairplayd.H2 relaunch constantly (no hardware).

**What is on the phone now:** userdata = image **v12** (`/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v12.img` + `.simg` + `.sha256`; v9 kept as fallback, v8/v10/v11 deleted or superseded). Kernel **0057** (`xnu/out-0057`, CI `39067532732`). ADT **`shim/out/sweet-0057hi.adt`** (QEMU twin `research/q0057-hi.adt`). Boot image **`shim/out/shim-boot-0057u.img`**. Boot args: `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1`.

**Build a phone image** (kernel K, ADT A): `python3 tools/mkpayload.py $K $A shim/out/payload-X.bin --cmdline "$B" && (cd shim && ./build.sh ../shim/out/payload-X.bin && cp out/shim-boot.img out/shim-boot-X.img)` then check `python3 tools/unpack_bootv2.py shim/out/shim-boot-X.img shim/out/check-X && cmp shim/out/check-X/ramdisk shim/out/payload-X.bin`. QEMU smoke test: `python3 tools/mkpayload.py $K research/q0057-hi.adt $S/csq/payloadX.bin --ramdisk $S/csq/root-fork.img --cmdline "debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=md0"` + `PATH=~/swift/usr/bin:$PATH python3 tools/qemu-sweet.py ... --secs 40 --kernel $K --fail-on-panic` (S = scratchpad; root-fork.img lives in the session scratchpad csq/ dir and may be gone in a new session).

**Kernel workflow:** edit `research/bt28/xnu/...` (keep the pre-edit copy in `research/preNNNN/`), syntax check `python3 tools/xnu-cc.py research/run28.log research/bt28 <file>`, make `xnu/patches/NNNN-*.patch` with `diff -u pre bt28 | sed` path rewrite, add new/changed files to `research/check-patch-order.py` list, run it (must say applied + MATCH), commit + push -> GitHub Actions `xnu` builds (~8 min) -> `gh run download <id> -R dipa-studio/ios-sweet -n xnu-kernel -D xnu/out-NNNN`. **Repo is PUBLIC since 2026-10-17** (private macOS runner minutes ran out: "recent account payments have failed"); public = free runners. Commit trailer: only `Co-Authored-By: dipa-studio <183933817+dipa-studio@users.noreply.github.com>`.

**Phone workflow (user does buttons, Claude runs commands):**
- Boot test kernel: phone in fastboot -> `fastboot boot shim/out/shim-boot-X.img`. NEVER `fastboot getvar`.
- **Read logs WITHOUT filming:** after a test the user holds Power+VolDown -> fastboot -> `fastboot boot recovery/recovery.img` (OrangeFox, temp) -> wait for `adb shell true` -> `tools/sweet-log-pull.sh /run/media/dipa/android/ios-sweet-work/logs/<name>.txt` (reads the 4 MiB console ring the kernel keeps in the last 1025 blocks of userdata; patch 0047). Then `adb reboot bootloader` brings the phone back to fastboot without the user.
- **Flash a root image:** `fastboot flash -S 512M userdata <img>.simg` (7 min, 25 chunks). After a big flash the Xiaomi ABL fastboot hangs: next `fastboot boot` stalls at "Sending" -> the USER must re-enter fastboot by hand (Power+VolDown). Do not retry commands against the hung ABL. A flash right after `adb reboot bootloader` once failed on chunk 1 (USB protocol error): just retry. User must NOT unplug during flash (once interrupted at 25/25 -> reflash).
- **Edit the phone root without reflashing (same-length only):** find the file's raw offset in the local image (it is one extent; search the image for its first 4 KiB), compare with `adb exec-out dd if=/dev/block/sda34 ...`, then `adb push` changed 4 KiB blocks and `dd of=/dev/block/sda34 seek=... conv=notrunc,fsync`, read back. Recovery kernel has no hfsplus.
- **Pull the whole phone root** (12 min): `adb exec-out "dd if=/dev/block/sda34 bs=1048576 count=16384" > img`; mount ro with udisksctl or `sudo mount -t hfsplus -o loop,ro`. To write into a pulled image: `sudo fsck.hfsplus -fy` first (volume is never cleanly unmounted -> Linux mounts ro even with -o force), then `sudo mount -t hfsplus -o loop,force`.
- Making images: copy latest img with `cp --sparse=always`, mount rw with force, change, `sync; umount`, `sha256sum`, `img2simg`. Keep only the latest image + one fallback on the SSD (/home is ~98% full; big work files live in `/run/media/dipa/android/ios-sweet-work/`, old kernels/images archived in `.../ios-sweet-work/archive/`).

**Diagnostics built into the kernel (all SWEET-only):** persistent console log (0047, boot-arg sweetlog=0 off; flush every ~1 s); `|name| text` copies of /dev/null writes for processes in `sweetnull` (default backboardd,SpringBoard; 0048); `IOK name:` IOKit lookup/open trace for `sweetiolog` (default backboardd,SpringBoard; 0049/0054, 400 lines max); `SWEET Sandbox: proc op N` (0050); `STR ...` heap text dump on SIGABRT/SIGTRAP for `sweetscan` (default backboardd,SpringBoard; 0051/0053) - this is how NSException/BSAssert reasons were found; `SMP name tN pc .. bt ..` user stack sampler for `sweetsample` (default SpringBoard,backboardd; first at 90 s then every 120 s, 40 samples, 16 frames; 0054/0055); `SWEET FB: N swaps` every 1000 swaps; `CRASH name ...` crash reports (0043/0044). Symbolicate cache addresses with `ipsw dyld a2s research/dsc/dyld_shared_cache_arm64 <addr - slide>`; main-binary addresses: addr - mh + 0x100000000.

**Patches added today (0046-0057, all CI-built):** 0046 IOSurface (IOCoreSurfaceRoot) + IOMobileFramebuffer (AppleCLCD on /arm-io/disp0) + arm-io nubs; 0047 persistent log; 0048 /dev/null tee; 0049 IOKit trace; 0050 permissive "Sandbox" MAC policy; 0051/0053 heap text on crash; 0052 RENAME_SWAP via exchangedata on HFS; 0054/0055 sampler, swap counter, AKS selectors 35 (extended device state = 17) and 101; 0056 panel copy from the vsync timer at 10 Hz (sweetfb_hz) instead of every swap; 0057 no config_ext_resolver (ACL group entries resolve without memberd). ADT (tools/mkadt.py): /arm-io/disp0, /chosen/display-scale 3, /chosen/unique-chip-id (synthetic 0x5357454554000001) + chip-id 0x8015 -> IOPlatformUUID A9683613-3704-53F2-95D0-98A8FB574E78. Image v9-v12: /private/var/containers/{Bundle,Data/System,Shared/SystemGroup,Temp}; launchd.plist StandardOut/ErrorPath renamed to ...PatX (pointing jobs at /dev/console stops them launching: EACCES); v12: LastSystemVersion=20H392 in plain /var/mobile/Library/Preferences/com.apple.migration.plist (stops the 1-hour data migration that ran every boot), VirtualAudio.plugin moved to /Library/Audio/Plug-Ins/HAL.disabled/.

**Codex helper** (`codex exec --skip-git-repo-check -m gpt-6.1-sol -c model_reasoning_effort=high -s workspace-write -C /run/media/dipa/android/ios-sweet-work/codex -o OUT "$(cat task.md)" < /dev/null`, run detached with setsid nohup; config already defaults to gpt-6.1-sol high). Reports in `/run/media/dipa/android/ios-sweet-work/codex/`: mediaserverd-report.md, installcoordinationd-report.md (-> 0057), ufs-performance.md (ranked UFS speed plan), **ufs/0058-sweet-ufs-faster.patch** (READY, not yet applied: ufslogsecs default 10 s log flush batching, optional ufscache=1 directional cache ops, optional ufswait=1 spin-then-sleep; syntax-checked, host-stub verified; see ufs/README.md). Apply it as patch 0058 after review.

**Claude safety-classifier note:** several messages about QuartzCore renderer selection / writing an HFS reader tool were cut off by an automatic classifier (false positives, user agrees). Keep those topics short or let them be; they are not blockers now (rendering already works).

**NEXT STEPS (in order):**
1. **Input** (Setup stuck on Hello): touch + power + volume as IOHIDEventService/IOHIDDevice services backboardd matches (log shows backboardd matching IOHIDDevice with DeviceUsagePairs and IOService with HIDServiceSupport=true). Phone hardware: Goodix/Focaltech touch over I2C/SPI (check live DT `dtb/`), buttons via PMIC (pon) and GPIO keys. Start with a fake HID service (e.g. power button via a timer or boot-arg) to learn the ABI, then real drivers.
2. **Device identity**: MobileGestalt reports iPod + Home button. Add /product node / product-type (iPhone10,3, D22AP), device class iPhone, home-button-less (Face ID) properties in mkadt; find exact MG keys (MGIOMFBSupport.c, `IODeviceTree:/product` lookups seen in backboardd trace).
3. **Battery**: AppleSmartBattery/IOPMPowerSource style service (status bar shows empty red battery).
4. Apply Codex 0058 (UFS speed), consider cpus>1, check whether installcoordinationd/mediaserverd loops are gone, analyse mobileactivationd.
5. Hide kernel console over the GUI when not debugging: boot-arg sweetfb_noconsole=1 (but then panics are not on screen).

- **🎉 MILESTONE: iOS GUI ON THE PHONE - Setup.app "Hello" screen (2026-10-17 ~21:30 UTC, boot 0057u on image v12).** Photo: `/run/media/dipa/android/ios-sweet-work/MILESTONE-setup-hello-2026-10-17.jpg` (animated "dobar d(an)" hello lettering, status bar "iPod" + battery icon, bottom "Pritisnite Home za otvaranje" = "Press Home to open" (locale hr), (i) button). Rendered by iOS 16.7 SpringBoard/Setup through CoreAnimation -> backboardd -> kernel IOSurface (IOCoreSurfaceRoot) -> kernel AppleCLCD swaps -> ABL panel memory. What made it: data migration finally skipped (v12: `LastSystemVersion` 20H392 in the plain `/var/mobile/Library/Preferences/com.apple.migration.plist`; ADT `/chosen/unique-chip-id` + `chip-id` for IOPlatformUUID), 0057 (no ext resolver -> ACL containers work), plus everything before (0046 display/IOSurface, 0050 Sandbox policy, display-scale 3, containers dirs, RENAME_SWAP, AKS 35/101, 10 Hz present).
  **Missing (next work, in rough order):**
  - **Input: no touch, no power button, no volume buttons** (no HID drivers; backboardd looks for IOHIDDevice / HIDServiceSupport services). Setup is stuck on "Hello".
  - **Device identity: it thinks it is an iPod with a Home button** (status bar "iPod", "Press Home to open"): MobileGestalt has no product/model identity (no /product node, product-type, etc.) - give it iPhone10,3 / home-button-less (Face ID class) identity.
  - **No battery detection** (no battery/charger/PMIC service; status bar battery shows empty/red).
  - Locale came up Croatian (hr) - harmless.
  - Performance: cpus=1, polled UFS (Codex UFS patch in progress: `codex/ufs/`), backboardd presents at 60 Hz.
- **README refreshed (2026-10-17):** read the full notes and updated the public overview through patch 0045, v7 hardware observations and prepared v8 diagnostics. Documents real Apple userspace, the missing display/IOSurface GUI work, limited security services, highmem, persistent userdata root, current tooling and build examples. Documentation only; no kernel build, tests, flashing or phone boot performed.

- **v11 still migrated (boot 0056u, 20:32 UTC, `logs/boot-0056u-v11.txt`).** On iOS kCFPreferencesCurrentHost lands in the plain plist: all DMEnvironment setters (setLastBuildVersionPref, setUserDataDispositionPref, ...) use CurrentHost and the phone's `/var/mobile/Library/Preferences/com.apple.migration.plist` holds DMUserDataDisposition; datamigrator's XPC service runs with `_RoleAccount: mobile`. **Image v12** = v11 + `LastSystemVersion` = 20H392 added to that plain `com.apple.migration.plist` (keeps DMUserDataDisposition, 501:501 0600) + `/Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin` moved to `/Library/Audio/Plug-Ins/HAL.disabled/` (Codex: stops the mediaserverd SIGSEGV loop; untested). **installcoordinationd loop explained (Codex `codex/installcoordinationd-report.md`):** os_crash "Failed to create data directory" EACCES for `/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library/InstallCoordination`: `Library`/`Library/Caches` in the container are ACL-only (uid/gid 0xfffffffe, mode 0000, ACE for group GUID ...012B = gid 299 systemusers); with `config_ext_resolver` the kernel asks an identity resolver (memberd/opendirectoryd) that iOS does not run -> EWOULDBLOCK -> ACE skipped -> EACCES. **Patch 0057**: drop config_ext_resolver from FILESYS_BASE (compatibility UID/GID<->GUID mapping is used instead), likely fixes other ACL-protected containers too. Codex task 3: UFS driver performance design (`codex/ufs-performance.md`).
- **mediaserverd SIGSEGV loop explained (Codex report `/run/media/dipa/android/ios-sweet-work/codex/mediaserverd-report.md`):** the fault (FAR 0x390) is in the HAL plug-in `/Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin/VirtualAudio` (ApplyBeamFormerPreference <- VirtualAudio_PlugIn::SetPropertyData <- HALS_AHPPlugIn::ObjectSetPropertyData <- AudioObjectSetPropertyData <- MediaExperience vaemVADSerializationListener): the default virtual audio device ('vdef', VirtualAudioDevice_Default) does not exist (no audio hardware/driver, no /product/audio). Proposed workaround for GUI bring-up: move VirtualAudio.plugin out of /Library/Audio/Plug-Ins/HAL/ in the next root image (untested). v11 reflashed OK after an interrupted flash (USB unplugged at chunk 25/25). Codex task 2: installcoordinationd crash loop.
- **ROOT CAUSE of the endless migration: no IOPlatformUUID (2026-10-17).** datamigrator's `-[DMMigrator isMigrationNeededWithBuildVersion:lastBuildVersion:clientPID:]` gets lastBuildVersion from `-[DMEnvironment lastBuildVersionPref]` = `CFPreferencesCopyValue("LastSystemVersion", "com.apple.migration", kCFPreferencesCurrentUser, kCFPreferencesCurrentHost)` -> a ByHost file. The phone had `/var/mobile/Library/Preferences/com.apple.migration.plist` (only DMUserDataDisposition) and NO `Preferences/ByHost` directory at all. On arm XNU publishes IOPlatformUUID (gethostuuid) only from `/chosen/unique-chip-id` (8 bytes) + `/chosen/chip-id` (4 bytes) (IOPlatformExpertDevice::generatePlatformUUID, called from IOPlatformExpert::start); the SWEET ADT had neither, so every CurrentHost preference write failed. Fix: mkadt adds `unique-chip-id` = 0x5357454554000001 (synthetic) and `chip-id` = 0x8015 -> UUID `A9683613-3704-53F2-95D0-98A8FB574E78` (same SHA1 namespace computation as the kernel). ADT `shim/out/sweet-0057hi.adt`, boot image `shim/out/shim-boot-0056u.img` (0056 kernel + 0057hi ADT, QEMU OK). **Image v11** = v10 + `Preferences/ByHost/com.apple.migration.<UUID upper and lower>.plist` {LastSystemVersion: 20H392} (501:501, dir 0755 files 0600). Flashing v11 2026-10-17 ~20:10 UTC. Codex (gpt-6.1-sol high) analysing the mediaserverd SIGSEGV loop in `/run/media/dipa/android/ios-sweet-work/codex/` (report `mediaserverd-report.md`).
- **v10 did NOT stop the migration (boot 0056n on v10, 19:18 UTC, `logs/boot-0056n-v10.txt`):** datamigrator + migrationpluginwrapper ran again, backboardd drew the progress UI (13000+ swaps in ~5 min). So `com.apple.DataMigration.plist`/LastSystemVersion is not the deciding input. What is known about the decision (datamigrator XPC service `DataMigration.framework/XPCServices/com.apple.datamigrator.xpc/com.apple.datamigrator`, strings): `Migration sentinel present. Migration necessary.` (sentinel `/private/var/datamigrator/migrate`, created and removed by datamigrator itself; absent in v10), `System build version changed from %@ to %@. Migration necessary.` / `...unchanged... not necessary`, `Disposition dictionary with build version %@ is out of date relative to current build version %@`, `SyntheticDidUpgrade`, DMUserDataDispositionManager; previous build source not found yet (not in the binary's plain strings; likely in DataMigration.framework's DMUserDataDispositionManager / preferences domain `com.apple.migration`). backboardd calls `DMPerformMigrationIfNeeded` and holds SpringBoard in `checkInAndWaitForDataMigration` until it returns. NEXT: find where the previous build version / disposition dictionary is stored (disassemble DataMigration.framework DMUserDataDispositionManager + datamigrator's isMigrationNeeded path), write it into the image; or get datamigrator's os_log out (OS_ACTIVITY_DT_MODE needs a launchd env change).
- **Data migration never records completion -> skip it (image v10, 2026-10-17).** Boot 0056n (18:16 UTC, `logs/boot-0056n.txt`, 30 min): 10 Hz panel copies, backboardd still presents at ~60 Hz (107k swaps), SpringBoard still in `BKSSystemShellService _checkIn` at sample 14. Every migration run takes 1+ hour on cpus=1 and repeats each boot. DataMigration decides from `/var/mobile/Library/Preferences/com.apple.DataMigration.plist` key `LastSystemVersion` (vs ProductBuildVersion 20H392); the pulled phone root (21:01 local) had NO such file. **v10** = that pulled phone state + fsck.hfsplus -fy (volume was marked not cleanly unmounted -> Linux mounts it read-only even with -o force; fsck found only hard-link counts + minor volume header, repaired) + `com.apple.DataMigration.plist` {LastSystemVersion: 20H392} (501:501 0600). `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v10.img` (+ .sha256, .simg); v8 files deleted, v9 kept as fallback.
- **Repo made PUBLIC (2026-10-17, user request) so GitHub-hosted macOS runners are free:** the 0056 CI run was refused ("recent account payments have failed or your spending limit needs to be increased"). Checked before: no device dumps, serials, IMEI or Apple binaries in the history (.gitignore keeps backup/, dtb/, recovery/, research/ out); commit author emails are public now. 0056 rerun `39056660104` passed -> `xnu/out-0056`, QEMU OK, image `shim/out/shim-boot-0056n.img` booted 18:16 UTC.
- **Boot 0055n (17:16 UTC, ~65 min, `logs/boot-0055n.txt`): migration bar reached 99% in ~30 min (was ~60), then crawled.** Root cause of the slowness: backboardd presents the progress UI at ~58 Hz (**167,000 swaps in ~48 min**) and AppleCLCD copied the full 10 MB frame into the panel on every swap -> ~600 MB/s memcpy on the single CPU (cpus=1), starving everything (polled UFS I/O too). Sampler (16 frames): **SpringBoard waits in `-[BKSSystemShellService _checkIn]`** (synchronous BSXPC call to backboardd, from FBSystemShellInitialize <- SBSystemAppMain) - backboardd holds the system shell check-in until the data migration ends, so the migration is the gate to SpringBoard UI. **Patch 0056**: swap only records the latest swap record; the vsync timer copies it to the panel every 60/sweetfb_hz ticks (default 10 Hz); `SWEET FB: N swaps` every 1000.
- **Boot 0054n (16:53 UTC, ~12 min, `logs/boot-0054n.txt`, patch 0054 sampler):** the data migration runs AGAIN on every boot (it completed once in the 0053s run) - completion is not being recorded. backboardd keeps swapping the progress UI (3700+ swaps in ~10 min, `SWEET FB: N swaps`). Sample during migration: SpringBoard main thread blocked in a synchronous XPC call (`mach_msg2_trap <- dispatch_mach_send_and_wait_for_reply <- xpc_connection_send_message_with_reply_sync <- -[BSXPCServiceConnectionMessage _sendSynchronously:] <- BSXPCServiceConnectionProxy ...`). Unsupported AKS selectors seen: 101 `aks_assert_consume`, 93 `aks_get_internal_info`, 34 `_aks_stash_load` (datamigrator), **35 `aks_get_extended_device_state`** (SpringBoard, chronod, remindd, searchd, symptomsd...). **Patch 0055**: AKS 35 answered like 17 (same DER device state), 101 succeeds; sampler backtraces 16 frames, 40 samples (~80 min).
- **MILESTONE: FIRST iOS PIXELS ON THE PANEL (boot 0053s long run, 15:33 UTC, ~60 min; photos IMG_20261017_183029/183222/183419; log `/run/media/dipa/android/ios-sweet-work/logs/boot-0053s-long.txt`, 407 KB, 1428 flushes).** The iOS data-migration boot screen (Apple logo + progress bar) was shown and the bar advanced to full in ~60 minutes. It is drawn by **backboardd through our kernel display path**: `SWEET SURF: backboardd create id 2/3 1080x2400 fmt 0x42475241 (BGRA) bpe 4 bpr 4352 size 10452992 keys 11`, IOSurface sels 9, 27, 5, 34 and traps 2/3 (lock/unlock), IOMFB SwapBegin (4) / SwapEnd (5, 1132-byte struct) / GetLayerDefaultSurface (3) / 68 / 55, then `SWEET FB: swap N layer 0 surface 2|3 1080x2400 ... a 0,0 1080x2400 b 0,0 1080x2400` alternating (double buffering; only the first 16 swaps are logged). Triggered by `com.apple.datamigrator` (AKS sels 101/93/34 return unsupported) and `com.apple.migrationpluginwrapper`. Probably ProgressUI-style CoreGraphics drawing into IOSurfaces (no GPU used). After the migration: SpringBoard and backboardd stay up (exec once each, no crash), nothing new drawn; crash loops only for mediaserverd, installcoordinationd, mobileactivationd (~108 each in the hour); bluetoothd and fairplayd.H2 relaunch constantly (no hardware). Kernel console text overlays the image (sweetfb_noconsole=1 would hide it).
- **MILESTONE: SpringBoard and backboardd both stay up (boot 0053s on v9, 15:29 UTC, `logs/boot-0053s.txt`).** With /chosen/display-scale = 3: `exec SpringBoard` once, `exec backboardd` once, no CRASH for either; MobileGestaltHelper reads IOMFB size/dot pitch; backboardd opens AppleCLCD (sels 57,15,22,29,33,79,74 as before). No IOSurface creates and no swaps: backboardd keeps polling `IOProviderClass IOAcceleratorES` (-> 0 found) and AGXAccelerator/IOAcceleratorESLegacy - no GPU, so no CoreAnimation renderer yet. Log goes quiet after ~47 s (only 47 flushes): the system idles. NEXT WALL: CoreAnimation rendering without a GPU (CPU path) so the window server produces frames.
- **SpringBoard's invalid mode = scale 0 (boot 0053n, 15:19 UTC, `logs/boot-0053n.txt`, heap scan):** `FBSDisplayMode cannot be configured as requested : <FBSDisplayMode: ...; infxinf@0x (1080x2400/0) 60Hz sRGB SDR>` - pixel size and 60 Hz are fine, scale is 0. libMobileGestalt (MGIOMFBSupport.c, sub_19c560a54) reads `display-scale` (u32, default 0) and `display-rotation` from **IODeviceTree:/chosen**, pixel size via IOMobileFramebufferGetDisplaySize (MobileGestaltHelper sel 8/28 in the log). mkadt now adds `/chosen/display-scale` = 3 (1080x2400 @3x = 360x800 pt). ADT `shim/out/sweet-0054hi.adt` (QEMU `research/q0054-hi.adt`), image `shim/out/shim-boot-0053s.img` = 0053 kernel + 0054hi ADT. Possible catch: MobileGestalt may have cached scale 0 in `/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/...`.
- **MILESTONE: backboardd survives and opens the display (boot 0052n on v9, 15:07 UTC, `logs/boot-0052n.txt`).** No `CRASH backboardd`. It walks `IODeviceTree:/arm-io` children, matches IOMobileFramebuffer, `open AppleCLCD type 0 -> 0x0`, calls IOMFB selectors 57, 15, 22, 29, 33, 79 (GetBlock, out 1580/40), 74 (SupportedFrameInfo), 12, 6, 27, 17, 84, 50; opens IOCoreSurfaceRoot (sel 13); looks for AGXAccelerator / IOAcceleratorES / IOAcceleratorESLegacy (none, repeatedly), `IODeviceTree:/product` (none, repeatedly), `/buttons`, aop-smart-cover, IOPMrootDomain. No swaps yet. **SpringBoard now gets past display discovery** and dies (sig 5) in `-[FBSDisplayMode _initWithWidth:height:preferredScale:scaleOverride:refreshRate:gamut:hdr:rotation:virtual:validityCheck:]+580` <- `_initWithCADisplayMode:scale:rotation:` <- `FBSDisplayConfiguration _initWithImmutableDisplay:` <- `FBSDisplaySource initWithDisplay:` <- `FBSDisplayMonitor` (slide 0x14504000): "FBSDisplayMode cannot be configured as requested". Checks: width/height >= 2, preferredScale > 0, scaleOverride >= 0, refreshRate non-zero, valid gamut/HDR/rotation. Suspects: refresh rate 0 (IOMFB timing blocks answered with zeros) and/or scale 0 (MobileGestalt main-screen-scale; no /product node in the ADT). **Patch 0053**: heap text scan also on SIGTRAP and for SpringBoard (default sweetscan list backboardd,SpringBoard) to read the exact mode description.
- **Image v9 (2026-10-17) = v8 + `/private/var/containers/{Bundle,Data/System,Shared/SystemGroup,Temp}` (root:wheel 0755; on iOS these live on the data volume, absent from the rootfs) + launchd.plist with StandardOutPath/StandardErrorPath renamed to ...PatX (the in-place phone fix; daemons' stdout reaches the log via the 0048 /dev/null tee).** `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v9.img` SHA256 aa9f9139...6e67, `.simg` 12,145,303,864 bytes; written through `sudo mount -t hfsplus -o loop,force`. **Patch 0052** (`xnu/patches/0052-sweet-hfs-rename-swap.patch`, CI `39042761826` -> `xnu/out-0052`, QEMU OK, image `shim/out/shim-boot-0052n.img`): vn_rename falls back to VNOP_EXCHANGE when VNOP_RENAMEX(RENAME_SWAP) returns ENOTSUP for two regular files on one volume (HFS exchange swaps data and cnode descriptors = swap semantics). Flashing v9 started 2026-10-17 ~15:00 UTC.
- **backboardd EXCEPTION FOUND (boot 0051n, 14:41 UTC, `logs/boot-0051n.txt`, patch 0051 heap scan):** `*** Terminating app due to uncaught exception 'NSInvalidArgumentException', reason: '*** -[NSURL initFileURLWithPath:]: nil string parameter'`. First-throw stack (slide 0x337f0000): `+[NSURL fileURLWithPath:]` <- `-[BSSystemContainerForCurrentProcessPathProvider libraryPath]` <- `cachesPath` <- backboardd main+0x1d858 (StartWindowServer, shader cache path). BaseBoard: libraryPath = `BSSystemSharedResourcesDirectoryForIdentifier` = `BSSystemSharedDirectoryForIdentifier(id)` + "/Library"; that calls `container_system_path_for_identifier` (libsystem_containermanager -> `_container_legacy_lookup` -> containermanagerd XPC), which returned NULL (BaseBoard logs "Error getting shared system container ..."). With 0050 the sandbox denials are gone. containermanagerd.log.0 in the pulled (pre-0050) image: "Did not receive any persona attributes", "Expected to find at least one persona", notify_register_dispatch failed 9; `/private/var/containers` does NOT exist in the root image (on iOS it lives on the data volume). usermanagerd does create kernel personas 99/199/1000, but fails to save `/private/var/keybags/usersession.kb` and `persona.kb`: "Failed to atomically swap writing file: Operation not supported (45)" (HFS has no RENAME_SWAP). NEXT: create `/private/var/containers` (and check what containermanagerd needs for system group containers), fix or emulate RENAME_SWAP on HFS, then re-check backboardd.
- **Disk housekeeping (2026-10-17):** /home was at 4 GiB free. Moved (not deleted) `xnu/out-0010..0044` and old `shim/out/*.img`/`payload-*.bin` (everything before 0046) to `/run/media/dipa/android/ios-sweet-work/archive/{xnu,shim-out}` (6.3 GiB); /home now 8.7 GiB free. Large work files (phone-root.img 16 GiB, logs, tracev3 dumps) live in `/run/media/dipa/android/ios-sweet-work/`. **Patch 0051** (`xnu/patches/0051-sweet-crash-heap-strings.patch`, CI `39041440935`): on SIGABRT of a process in boot-arg `sweetscan` (default backboardd) the crash log prints heap text runs as `STR ...` (to find the NSException reason). Boot 0051n 14:41 UTC.
- **Boot 0049n (14:11 UTC, `logs/boot-0049n.txt`, patch 0049 trace): backboardd's IOKit lookups before it dies** (each run): `IODeviceTree:/arm-io/pearl-sep` -> none, matching ApplePearlSEPDriver -> none, `IOService:/IOResources/AppleKeyStore` open OK, matching IOHIDDevice (DeviceUsagePairs) / `HIDServiceSupport` notifications, `usb-drd,t8027` notification; **never any IOMobileFramebuffer / display lookup** - the exception happens before display detection. launchd (tracev3) shows backboardd's mach lookups denied with `error = 159: Sandbox restriction`: com.apple.mobilegestalt.xpc, com.apple.iohideventsystem, com.apple.system.notification_center, com.apple.logd, containermanagerd, ACCHWComponentAuthService, oscard. libsystem_sandbox uses `__mac_syscall("Sandbox", op, arg)` (ops: 1 init, 2 check, 3 note, 4 container path, 5/6/7/8 extensions, 0x15 check_bulk, ...); with no policy named "Sandbox" every call gets ENOPOLICY. **Patch 0050** (`xnu/patches/0050-sweet-sandbox-policy.patch`): built-in permissive MAC policy "Sandbox" in sweet_amfi.c (mpo_policy_syscall returns 0, container/extension ops ENOENT; logs first use of each op as `SWEET Sandbox: proc op N`).
- **Phone root pulled (16 GiB, 12 min, `adb exec-out dd if=/dev/block/sda34`) to `/run/media/dipa/android/ios-sweet-work/phone-root.img` (loop-mounted ro at /run/media/dipa/iOSRoot).** No .ips crash reports. logd tracev3 (Persist, LZ4 `bv41` blocks, decompressed strings in `ios-sweet-work/tracev3/all.txt`): /private/var/db/uuidtext is EMPTY so only launchd's own messages are readable. Findings: v8 launch failure = `Service could not initialize: posix_spawn(/usr/libexec/backboardd), error 0xd - Permission denied` (xpcproxy exit 78) - /dev/console not openable for the mobile user; backboardd[98] spawned `com.apple.MTLCompilerService` before SIGABRT; **launchd denies many mach lookups with `error = 159: Sandbox restriction`** (mediaserverd -> com.apple.backboard.display.services, remoted, mobiletimerd -> sleepd, identityservicesd -> apsd) and **tccd exits OS_REASON_SANDBOX "profile not found"** - no Sandbox kext/profiles on SWEET; likely cause of many daemon crash loops (next wall after the display). CF's uncaught-exception text never reaches stderr for daemons (no CF handler; libc++abi prints only the type). **Patch 0049** (`xnu/patches/0049-sweet-iokit-trace.patch`): `IOK name: ...` console lines for registry path lookups, child iterators, matching (with result count/class), matching notifications and IOServiceOpen of the processes in boot-arg `sweetiolog` (default backboardd), max 400 lines.
- **CONFIRMED: launchd StandardOut/ErrorPath=/dev/console stops backboardd from launching.** After the in-place rename (boot 0047n-b, log `logs/boot-0047n-b.txt`) backboardd launches again and dies as on v7: sig 6, `___pthread_kill <- abort <- demangling_terminate_handler <- _objc_terminate <- std::terminate <- _dispatch_client_callout <- _dispatch_once_callout <- main+0x9334` (window-server dispatch_once). It never opened AppleCLCD or IOCoreSurfaceRoot (no `SWEET FB/SURF: backboardd` lines) -> throws before/at display detection. **Patch 0048** (`xnu/patches/0048-sweet-devnull-tee.patch`, CI `39037749668` -> `xnu/out-0048`, image `shim/out/shim-boot-0048n.img`): writes to /dev/null by processes named in boot-arg `sweetnull` (default `backboardd,SpringBoard`) are printed as `|name| text`. Boot 0048n (13:48 UTC, log `logs/boot-0048n.txt`): only `|backboardd| libc++abi: terminating due to uncaught exception of type NSException` (CF's reason line goes to os_log, not stderr, despite CFLOG_FORCE_STDERR/OS_ACTIVITY_DT_MODE). Next: pull the whole 16 GiB root from recovery (`adb exec-out dd if=/dev/block/sda34`) to `/run/media/dipa/android/ios-sweet-work/phone-root.img` and read ReportCrash .ips files / logd tracev3 for the exception reason.
- **Persistent log WORKS (boot 0047n on v8, 13:31 UTC; pulled from OrangeFox: 70 KB, 111 flushes, `/run/media/dipa/android/ios-sweet-work/logs/boot-0047n.txt`).** In 3 min **backboardd was never exec'd** (v7 ran it), ~70 bare `exec xpcproxy`, SpringBoard loops on FBSDisplayMonitor (no display), mediaserverd sig 11 loop, locationd, lockdownd sig 5. Suspect the v8 plist (StandardOut/ErrorPath=/dev/console on backboardd). **In-place plist edit from recovery (no reflash):** launchd.plist is one extent at image/partition byte offset 0x1e025f000 (195 blocks; verified bytes identical on phone vs local v8 mount); renamed StandardOutPath->StandardOutPatX and StandardErrorPath->StandardErrorPatX (same length, both jobs) in blocks 68 and 105 via `adb push` + `dd of=/dev/block/sda34 seek=... conv=notrunc,fsync`, read back VERIFIED. Phone now differs from local v8 image in those 2 blocks (files in `/run/media/dipa/android/ios-sweet-work/`). Test boot 0047n 13:38:42 UTC. Recovery kernel has no hfsplus (raw block edits only).
- **Patch 0047: persistent console log (CI `39036478663` -> `xnu/out-0047`, QEMU no panic; phone image `shim/out/shim-boot-0047n.img` = 0047 + sweet-0046hi.adt + 0045n cmdline).** `_cnputs` (all console output: kernel printf + daemons' /dev/console) also appends to a 4 MiB ring in sweet_ufs.c; after the UFS partition is ready and writable (ufsrw=1) a thread writes new ring blocks + header to the LAST 1025 blocks (4 KiB) of userdata every ~1 s and SYNCHRONIZE CACHE (prints `SWEET LOG: console log to the last 4100 KiB ...`). Read: phone to fastboot -> `fastboot boot recovery/recovery.img` (OrangeFox) -> `tools/sweet-log-pull.sh OUT.txt` (adb dd of that area + `tools/sweet-log.py`). Not yet verified on the phone. Panics after the last flush are not in it (screen still shows them). 2x font attempt reverted: XNU's v_scale only scales the progress icon.
- **Boot 0046n on v8 (13:05 UTC, VID_20261017_150522_HSR_120):** phone prints `SWEET SURF: IOCoreSurfaceRoot registered` and `SWEET FB: AppleCLCD 1080x2400 stride 4320 at 0x9c000000, default surface 1` (nubs + match work on hardware). Visible userspace: early boot tasks, launchd_cache_loader, `exec SpringBoard`; no `exec backboardd` / `SWEET FB: backboardd` line seen in the readable frames (video blurry). Frame 00:47: lower half of the screen shows OLDER console text (16:01:12) below newer lines -> unconfirmed hint that a swap copied an old snapshot of the panel (CA reads the default/displayed surface = panel memory). SpringBoard still dies in `-[FBSDisplayMonitor _initWithBookendObserver:transformer:]+1160` (sig 5, slide 0x30960000). **Shim: 2x console font** (`shim/loader.c` Video.v_depth scale field = 1) so phone videos are readable; image `shim/out/shim-boot-0046b.img` = 0046n + 2x font.
- **Patch 0046: built-in IOSurface + IOMobileFramebuffer (2026-10-17, offline, NOT phone-tested).** `xnu/patches/0046-sweet-iosurface-iomfb.patch`: new `iokit/Kernel/arm/SweetSurface.{h,cpp}` (IOCoreSurfaceRoot + IOSurfaceRootUserClient: sel 13/0/4/34/35, traps 0-5, wired buffers mapped into the caller, per-client refs) and `SweetFramebuffer.cpp` (AppleCLCD : IOMobileFramebuffer on /arm-io/disp0: sizes, swap = CPU copy of enabled layers into the ABL panel memory, 60 Hz vsync via sendAsyncResult64WithOptions(CanDrop), panel memory published as the default IOSurface, kernel text console paused on the first swap only with `sweetfb_noconsole=1` (default keeps the console: pausing switches it to serial and would hide panics)); SweetPlatformExpert creates nubs for /arm-io children (not gic) attached to the platform expert (IOPlatformDevice::compareName casts its provider to IOPlatformExpert, so nubs under the arm-io nub never matched - first CI build showed that in QEMU). mkadt: /arm-io/disp0 {device_type display-subsystem, compatible sweet,display, canvas-width 1080, canvas-height 2400}. CI `39021572169` -> `xnu/out-0046`. **QEMU** (`research/q0046-hi.adt`, RAM disk init, msgbuf): `SWEET SURF: IOCoreSurfaceRoot registered`, `SWEET FB: AppleCLCD 1080x2400 stride 4320 at 0x9c000000, default surface 1`, init alive, no panic. Phone image **`shim/out/shim-boot-0046n.img`** (SHA256 9c4d2fe9..., CI `39024108588` with the review fixes: swap bounds check, zeroed 64-bit async ref, port release, 1.5 GiB wired budget, ref leak/race fixes; QEMU re-checked) = 0046 kernel + `shim/out/sweet-0046hi.adt` + the 0045n cmdline; boot it on image v8 (backboardd/SpringBoard log to console) and film. Expected new console lines: `SWEET SURF: <proc> create id ...`, `SWEET FB: backboardd sel N ...`, `SWEET FB: swap ...`. Open question for the next wall: QuartzCore's IOMFB server builds its renderer from Metal first (`CAMetalContextCreate`), GLES only when enabled; how the CPU renderer gets selected on a device without a GPU is not yet established.
- **IOSurface values ABI (offline RE, `/run/media/dipa/android/ios-sweet-work/iosurface-values.md`) - TODO in SweetSurface (0046 only accepts and drops values, so IOSurfaceCopyValue always returns NULL):** sel 9 SetValue in = {u32 id, u32 pad, IOCFSerialize([value, key]) or ([dict] = merge)}, sel 0xb RemoveValue in = {u32 id, u32 pad, key\0} (empty key = remove all); both out 4 bytes = new generation, client accepts only (gen+2)|1. Kernel after each change: gen = (gen+2)|1 (odd, never 0) -> state+0x20, serialized size (>=1) -> state+0x28. sel 0xa in {u32 id, u32 0, u8 0}, out {u32 ?, u32 gen, serialized non-bulk dict}; too small -> 0xe00002db + needed size (client retries x16). sel 0x1b in 0x88: bulk struct 0x70 @0, u64 valid size @0x70, u64 changed mask @0x78, u32 id @0x80 -> copy masked fields into state+0x30 (bit0 32B @0, bit1 16B @0x20, bit2 8B @0x30, bits3-10 1B @0x38..0x3f, bits11-14 24B @0x40/4B @0x58/8B @0x5c/1B @0x64).
- **IOMFB display matching confirmed (offline RE, `/run/media/dipa/android/ios-sweet-work/iomfb-match.md`):** the framework never matches on "AppleCLCD"; any registered IOMobileFramebuffer subclass works. `iomfb_match_callback` reads only `dfr`/`AppleTV`/`external` (CFBoolean, service itself, no parents); internal main display = all absent/false (type 0, name "primary"). Display count = /arm-io children whose device_type starts with "display-subsystem" (count 0 -> CreateDisplayList returns NULL; more nodes than services -> 30 s wait). IOMobileFramebufferOpen fails only on IOServiceOpen failure; canvas-width/height read as 4-byte CFData from the IOService-plane parent (optional). CopyProperty = IORegistryEntrySearchCFProperty on the service only. Patch 0046 (disp0 node, `external`=false, AppleCLCD : IOMobileFramebuffer) fits all of this.
- **IOMobileFramebuffer swap + notification ABI (2026-10-17, offline).** Swap record = fb+0x18, **0x46c bytes**, zeroed by SwapBegin and sent whole by `kern_SwapEnd` = IOConnectCallStructMethod(sel 5, rec, 0x46c, no output). **SwapBegin** = sel 4, no input, 1 scalar out = swap id (stored at rec+0x50). `kern_SwapSetLayer(fb, layer<4, IOSurfaceRef, CGRect a, CGRect b, flags)`: rec+0x54+4*layer = **IOSurface id**; rec+0x104 bit layer = layer enabled (rec+0x108 bit cleared); rect a (int x,y,w,h, scaled) at rec+0x64+16*layer, rect b at rec+0xc4+16*layer; rec+0x114+4*layer = transform from flags&0xf; flag bits 5/31/6/7/8/9 set bytes rec+0x2ca/0x2cc/0x2e3/0x2e5/0x2e6/0x467+layer. A NULL surface only sets the enable bit with zero id. SwapWait = sel 6 scalars [swap id, timeout, 0].
  - **Notifications**: `IOMobileFramebufferScheduleWithDispatchQueue`/`GetRunLoopSource` create an IONotificationPort and `IOConnectSetNotificationPort(conn, type, port, 0)` (kernel `registerNotificationPort`). `kern_EnableNotifications(fb, type, cb, ctx)` = **sel 72 scalars [NotifyCallbackFuncs[type], fb, type, 0]** -> the kernel keeps an async reference {port, func, refcon=fb} per type and fires it with `sendAsyncResult64`. Types: 0 HotPlug, 1 Power, 3 FrameInfo (args[0] = mask of 31-bit keys, args[1] u32, then values), 4 CRC, **5 Vsync**: 6 args, each a 32-bit half in a u64 slot: (ts lo, ts hi), (t2 lo, hi) passed through the time-ingest helper, (t3 lo, hi); the framework then calls the client's vsync callback(fb, ts, t2', t3, ctx). Vsync callbacks are dropped while the notification port has queued messages.

- **IOSurface.framework user-client ABI reversed (2026-10-17, offline, step 2 of the GUI plan).** `ipsw dyld disass research/dsc/dyld_shared_cache_arm64 --image IOSurface` (selector scan script: scratchpad `selscan.py`).
  - Connect (`_iosConnectInitalize`, pthread_once): `IOServiceGetMatchingService(IOServiceNameMatching("IOCoreSurfaceRoot"))`, retried with sleep(1) up to 241 times (a missing service stalls every client ~4 min, no crash); `IOServiceOpen(type 0)`; then **sel 13** (no inputs, struct out 0x18 bytes = `_ioSurfaceLimits`) and requires u16 at out+0x16 == 1 (ABI version); out+0 = offset/plane-base alignment mask, out+4 = bytes-per-row alignment mask (used as (x+m)&~m), +0..+0x14 also copied into IOSurface remote-method init, otherwise logs and closes the connection.
  - **Create = sel 0**: scalar in [parent id or 0], struct in = `IOCFSerialize(props, kIOCFSerializeToBinary)` (OSUnserializeBinary format; keys IOSurfaceWidth/Height/BytesPerRow/BytesPerElement/PixelFormat/AllocSize/PlaneInfo/CacheMode/IsGlobal/...), struct out **0xa68 bytes** ("lock result"). **Lookup = sel 4**: scalar in [id], same struct out. LookupFromMachPort = sel 0x22 (scalar in [port name]); CreateMachPort = sel 0x23 (scalar in [id, options], scalar out [port name]).
  - Lock result (copied to client+0x68, size 0x68 + planes*0x80, min 0xe8): 0x00 u64 base address **mapped in the caller's task**; 0x08 u64 task VA of a per-surface shared state page (+0 u64 registry id, +8/+0xa u16 bound accel ids, +0xc u32 seed, +0x14 u8 tiled, +0x20 u32 value generation, +0x24 YCbCr matrix, +0x28 u32 non-bulk values size, +0x2d u8 displayable, +0x2e u8 sysmem-only, +0x30 0x70 bytes bulk attachments); 0x10 u64 task VA of timestamp page (4 u64 indexed timestamps, +0x20 detach code); 0x18 u32 surface id; 0x1c u32 alloc size; 0x20 width; 0x24 height; 0x28 bytes per row; 0x2c buffer offset; 0x30 pixel format; 0x34 u32 (dcache flush length); 0x38 plane count; 0x40 u16 bytes/element; 0x42 u8 element width; 0x43 u8 element height; 0x44 cache mode; 0x4c u8 subsampling; 0x4d u8 prohibit-use count; 0x50 parent id; 0x54 slice count; 0x58 u64 trace id; 0x60 u64 protection options; planes[n] at 0x68+n*0x80 (+0 width, +4 height, +8 bytes/row, +0xc offset, +0x14 size, +0x18 u16 bpe, +0x1a/+0x1b u8 element w/h, +0x1c u8 component count, +0x1d address format, +0x54 u8 compression type (non-zero -> no CPU base address)).
  - **Traps** (IOConnectTrapN -> `IOUserClient::getTargetAndTrapForIndex`): 0 IncrementUseCount(id, category), 1 DecrementUseCount(id, category), 2 **Lock**(id, options, &seed_out), 3 **Unlock**(id, options, &seed_out), 4 Release(id), 5 Release after a failed create/lookup setup (id).
  - Other selectors: 5 SetYCbCrMatrix, 6 WrapClientImage, 7 WrapClientMemory, 9 SetValue (serialized), 0xa UpdateNonBulkValueCache (struct in 9), 0xb RemoveValue, 0xc BindAccel, 0x11 SetSurfaceNotify (async), 0x12 RemoveSurfaceNotify, 0x13 Log, 0x14 SetPurgeable, 0x15 SetOwnership, 0x16 SetTiled, 0x18 SetTimestamp, 0x19 GetTileFormat, 0x1a CopyDataValueBytes, 0x1b SetBulkAttachments (struct 0x88), 0x1e/0x2e-0x31 (via outlined helpers), 0x1f SetCompressedTileDataRegionMemoryUsed, 0x20 GetGraphicsCommPageAddress, 0x21 SetIndexedTimestamp, 0x24-0x26/0x2d IOSurfaceAccelerator, 0x27-0x29 shared events, 0x2a SignalEvent, 0x2b Set/InitDetachModeCode, 0x2c SetOwnershipIdentity, 0x32-0x34 data properties.
  - => SWEET kernel service: class/name **IOCoreSurfaceRoot** + IOSurfaceRootUserClient; minimum = sel 13, 0, 4, 9/0xa/0xb (values), traps 0-5, 0x22/0x23 later for cross-process surfaces (CA sends surfaces to backboardd by mach port). Memory: IOBufferMemoryDescriptor per surface, mapped into each task with createMappingInTask; ids from a global table.

- **Image v8: daemon logs on the console.** `tools/patch-launchd-plist.py IN OUT LABEL...` rewrites launchd.plist (binary, plistlib): renames every `_PanicOnCrash` to `_PanicOnCrasX` and gives the listed jobs `StandardOutPath`/`StandardErrorPath` `/dev/console` (devfs console is 0622, writable by mobile) plus `OS_ACTIVITY_DT_MODE=YES` (os_log mirrored to stderr) and `CFLOG_FORCE_STDERR=1`. v8 = v7 + that plist for com.apple.backboardd and com.apple.SpringBoard, written through the mount (796310 bytes, root:80 0644), SHA256 in `ios16-root-20H392-v8.img.sha256`, `.simg` ready; boot with `shim/out/shim-boot-0045n.img`. Goal: see backboardd's uncaught exception text and the window server's display probing. ODDITY (unresolved): the v5 raw-image rename at offset 0x1e025f000 is present in the v5-v7 image bytes, but the plist read through a Linux mount of v7 still had the original `_PanicOnCrash` keys; filefrag shows the file as one extent. Never patch file contents through raw offsets again; write through the mount.
- **IOMobileFramebuffer.framework reversed (step 1 of the GUI plan).** `ipsw dyld disass research/dsc/dyld_shared_cache_arm64 --image IOMobileFramebuffer`.
  - Display discovery (`IOMobileFramebufferCreateDisplayList` -> `getDisplayListNumber`): iterates children of `IODeviceTree:/arm-io`, counts nodes whose `device_type` is "display-subsystem" or "ext-display-subsystem" (or `compatible` "paravirtualizedgraphics,gpu"); then `iomfb_populate_display_infos` adds an IOServiceAddMatchingNotification(IOServiceFirstMatch, **IOServiceMatching("IOMobileFramebuffer")**) and runs the run loop up to 30 s until that many services matched. `iomfb_match_callback` reads service properties `dfr`, `AppleTV`, `external` (CFBoolean) and names displays (external ones "%s-%d").
  - `IOMobileFramebufferOpen(service, task, type, &fb)`: `__ioMobileFramebufferAlloc` (CFPreferences com.apple.iokit.IOMobileGraphicsFamily: canvas_width/height, override_display_width/height, override_panel_width/height, benchmark, enable_ktrace), `IOServiceOpen(service, task, type)` (GetMainDisplay passes type 0), `getEDTProperty` reads `canvas-width`/`canvas-height` from the service's IODeviceTree parent, then fills a table of `kern_*` function pointers.
  - User client selectors (`kern_*` -> IOConnectCall*): 3 _kern_GetLayerDefaultSurface[ScalarMethod_stub]; 4 _kern_SwapBegin[ScalarMethod_stub]; 5 _kern_SwapEnd[StructMethod_stub]; 6 _kern_SwapWaitWithTimeout[ScalarMethod_stub] _kern_SwapWait[ScalarMethod_stub]; 7 _kern_GetID[ScalarMethod_stub]; 8 _kern_GetDisplaySize[ScalarMethod_stub]; 10 _kern_SetTVOutMode[ScalarMethod_stub]; 11 _kern_SetWSSInfo[ScalarMethod_stub]; 12 _kern_RequestPowerChange[ScalarMethod_stub]; 13 _kern_EnableStatistics[ScalarMethod_stub]; 14 _kern_SetFlags[ScalarMethod_stub]; 15 _kern_SetDebugFlags[ScalarMethod_stub]; 16 _kern_SetTVOutSignalType[ScalarMethod_stub]; 17 _kern_SetGammaTable[StructMethod_stub]; 18 _IOMobileFramebufferIsMainDisplay[ScalarMethod_stub]; 19 _kern_SetWhiteOnBlackMode[ScalarMethod_stub]; 20 _kern_SwapSignal[ScalarMethod_stub]; 22 _kern_SetDisplayDevice[ScalarMethod_stub]; 23 _kern_SetDigitalOutMode[ScalarMethod_stub]; 24 _kern_GetHDCPDownstreamState[StructMethod_stub]; 25 _kern_GetDigitalOutState[ScalarMethod_stub]; 26 _kern_printDisplayRegs[ScalarMethod_stub]; 27 _kern_GetGammaTable[StructMethod_stub]; 28 _kern_GetDotPitch[ScalarMethod_stub]; 29 _kern_GetDisplayArea[StructMethod_stub]; 30 _kern_EnableDisableDithering[ScalarMethod_stub]; 31 _kern_SetUnderrunColor[ScalarMethod_stub]; 33 _kern_EnableDisableVideoPowerSavings[ScalarMethod_stub]; 34 _kern_SetVideoDACGain[ScalarMethod_stub]; 35 _kern_SetLine21Data[ScalarMethod_stub]; 41 _IOMobileFramebufferEnableMirroring[ScalarMethod_stub]; 42 _IOMobileFrameBufferGetMirroringCapability[StructMethod_stub]; 43 _IOMobileFramebufferSetRenderingAngle[StructMethod_stub]; 44 _IOMobileFramebufferSetMirrorContentRegion[StructMethod_stub]; 47 _kern_HDCPSendRequest[ScalarMethod_stub]; 48 _kern_HDCPGetReply[ScalarMethod_stub]; 49 _kern_SurfaceIsReplaceable[ScalarMethod_stub]; 50 _kern_SetBrightnessCorrection[ScalarMethod_stub]; 51 _kern_SetColorRemapMode[ScalarMethod_stub]; 52 _kern_SwapCancel[ScalarMethod_stub]; 53 _IOMobileFramebufferSetCanvasSize[ScalarMethod_stub]; 54 _IOMobileFramebufferGetMatrix[Method_stub]; 55 _IOMobileFramebufferSetMatrix[Method_stub]; 56 _IOMobileFramebufferKernelTests[StructMethod_stub]; 57 _kern_GetColorRemapMode[ScalarMethod_stub]; 66 _kern_SPLCSetBrightness[ScalarMethod_stub]; 67 _kern_SPLCGetBrightness[ScalarMethod_stub]; 68 _kern_SetParameter[Method_stub]; 69 _kern_SwapSetColorMatrix[StructMethod_stub]; 72 _kern_DisableNotifications[ScalarMethod_stub] _kern_EnableNotifications[ScalarMethod_stub]; 73 _kern_ChangeFrameInfo[ScalarMethod_stub]; 74 _kern_SupportedFrameInfo[Method_stub]; 75 _kern_FactoryPortal[StructMethod_stub]; 77 _kern_GetCurrentAbsoluteTime[ScalarMethod_stub]; 78 _kern_SetBlock[Method_stub]; 79 _kern_GetBlock[Method_stub]; 80 _kern_GetLinkQuality[ScalarMethod_stub]; 81 _kern_SwapCancelAll[ScalarMethod_stub]; 82 _kern_GetProtectionOptions[ScalarMethod_stub]; 83 _kern_CopyLayerDisplayedSurface[ScalarMethod_stub]; 84 _kern_SetContrast[StructMethod_stub]; 85 _kern_GetBufBlock[Method_stub]; 86 _IOMFBGainEncoderFinishEncoding[Method_stub]; UNKNOWN {}
  - => Kernel needs: an `/arm-io` child node with device_type "display-subsystem" (+ canvas-width/height) and an IOService of class **IOMobileFramebuffer** (subclass name match) attached under it, opened with type 0, implementing at least GetDisplaySize(8), IsMainDisplay(18), GetID(7), GetLayerDefaultSurface(3), SwapBegin(4)/SwapEnd(5 struct)/SwapWait(6), Enable/DisableNotifications(72), frame info(73/74), CopyLayerDisplayedSurface(83), plus IOSurface for the swapped buffers. NEXT: CoreAnimation's AppleDisplay/IOMFBDisplay usage of these (constructor, first swap, vsync), then IOSurface.framework's IOSurfaceRoot selectors.
- **GUI PLAN (decided 2026-10-16): CPU rendering + kernel IOMobileFramebuffer.** User: CPU rendering is fine for now (goal = prove the GUI runs), may later research an Apple-style GPU driver for the Adreno 618 -> keep display output and renderer separable. QuartzCore findings (symbols from the cache's .symbols subcache: `ipsw dyld symaddr research/dsc/dyld_shared_cache_arm64 --image QuartzCore`; strings: `ipsw dyld macho ... QuartzCore --strings`):
  - Renderers: `sw_new_context` (CA::OGL::SWContext, full CPU renderer incl. blur, images, IOSurface-backed surfaces, make_buffer_current(WindowServer::Surface)), `gles_new_context`, `metal_new_context`, `new_null_context`. Env knobs include CA_ENABLE_OGL, CA_DISPLAY_TYPE, CA_FORCE_LOCAL_SERVER, CA_FORCE_TIMER, CA_CANVAS_WIDTH/HEIGHT, CA_MAIN_DISPLAY_SCALE/ORIENTATION, CA_DISABLE_RENDER, CA_ALWAYS_RENDER (selection logic not traced yet).
  - `-[CAWindowServer _detectDisplays]` (0x1884e3150) iterates static tables `open_funcs`/`count_funcs` = AppleCLCDDisplay, AppleRGBOutDisplay, AppleWirelessDisplay only; each `count()` = `display_count_of_type(IOMFBDisplayType)` over **`IOMobileFramebufferCreateDisplayList()`**, `open(i)` = `create_display_of_type` -> `CA::WindowServer::AppleDisplay(CFString name, ..., IOMFBDisplayType)` (an IOMFBDisplay driven by IOMFBServer: vsync_callback, frame_info_callback, swaps). `CA::WindowServer::VirtualDisplay` exists but is never auto-detected (explicit API only). => the main display MUST come from an IOMobileFramebuffer kernel service.
  - NEXT: reverse IOMobileFramebuffer.framework: IOMobileFramebufferCreateDisplayList / IOMobileFramebufferOpen (service matching, display type property), the user-client selectors used by IOMFBServer (display size/modes, swap begin/set-layer/end with IOSurface ids, vsync notifications), then the IOSurface kernel side (IOSurfaceRoot user client: create/lookup/lock, memory mapping) that both IOMFB swaps and CA surfaces need. Kernel: SWEET IOMobileFramebuffer service that copies the swapped IOSurface to the 1080x2400 framebuffer at 0x9c000000.
- **logd fixed; backboardd = DISPLAY WALL (boot 0045n on v7, 21:08:57 UTC, frames VID_20261016_230850 00:55-01:28, slide 0x4c3ec000, `research/crash-0045n-v7.txt`).** No `CRASH logd` any more; new daemons run (runningboardd, WirelessRadioManagerd, networkserviceproxy, dasd, cloudpaird, magicswitchd, atc, askpermissiond, ManagedSettingsAgent, containermanagerd uses AKS 0/17). New: locationd sig 11 at CFRunLoopAddSource+132 (NULL source) from its main+0x2af138 in a dispatch_once. **backboardd** sig 6: abort <- std::terminate <- _objc_terminate <- dispatch_once <- main+0x9334 = the "start %{public}@..." wrapper (0x100009264) for **"window server"** (block 0x10000a770 -> dispatch_once body 0x10001d7f0 = StartWindowServer): MTLSetShaderCachePath, `[CAWindowServer serverWithOptions:@{kCAWindowServerDisableUpdatesOnMainDisplay: @YES}]`, `setRendererFlags:`, **`mainDisplay`** -> `displayId` -> `[CADisplay displayWithDisplayId:]`, setWindowServerDisplay:, bounds, pointScale, currentMode, preferredScale, nativeOrientation, touchScales... With no display, mainDisplay is nil and it throws. (HID system and GS start fine before it.) SpringBoard dies on the same missing display (FBSDisplayMonitor). QuartzCore display backends (from the cache's .symbols subcache, `ipsw dyld str`): `CA::WindowServer::AppleCLCDDisplay` (IOMobileFramebuffer hardware) and **`CA::WindowServer::VirtualDisplay`** (open(), new_server, clone_update). `CA_DISPLAY_TYPE` and `CA_FORCE_LOCAL_SERVER` strings exist in QuartzCore but have no direct xref (likely read via a table of CA_* env names). NEXT: find how CA::WindowServer::Server enumerates displays (IOMobileFramebuffer services vs VirtualDisplay / CA_DISPLAY_TYPE) and choose: (a) kernel IOMobileFramebuffer service + IOSurface, or (b) a VirtualDisplay rendered by CA's CPU path (CA::OGL::SW) and copied to the framebuffer.
- **watchdogd + CommCenter + identityservicesd fixed (boot 0045n on v6, 20:41:30 UTC, frames VID_20261016_224133 00:39-01:13, slide 0x20fec000).** `SWEET WDT: watchdogd sel 0 in 0/0 out 1/0 -> ok`, no watchdogd/CommCenter/identityservicesd crashes (CommCenter/identityservicesd now call AKS 0/17/7). usermanagerd boot task runs (UMD_INIT creates primary session uid 501 "mobile", personas 99 System and 199 Default registered with the kernel); `Failed to atomically swap writing file /private/var//keybags/usersession.kb: Operation not supported (45)` (HFS lacks the atomic swap rename; non-fatal so far). Remaining: SpringBoard (FBSDisplayMonitor, no display), backboardd (uncaught exception), mediaserverd, lockdownd (crash inside obfuscated code, main+0x4c1b4?), logd. **logd**: main+0x18160 = `BUG IN LIBTRACE: Failed to migrate directory` from 0x10001801c(dir 0): 0x100016538 requires the dir to exist with uid/gid/mode from the table at 0x10002df14 (0: /private/var/db/diagnostics 272:272 0751, 1: /private/var/db/uuidtext 272:272 0755, 2: /private/var/db/diagnostics/timesync 272:272 0755) plus a `.migrated` file, else XPC "operation 6" to a helper and crash. **Image v7** = v6 + those dirs/markers (also in mkiosroot.sh), SHA256 in `ios16-root-20H392-v7.img.sha256`, `.simg` ready. Boot with `shim/out/shim-boot-0045n.img`.
- **Exit-reason text works (boot 0044n, frames VID_20261016_221726 00:32-00:53, slide 0x22d5c000).** CommCenter/identityservicesd: `couldn't dlopen libobjc-trampolines.dylib: dlopen(/usr/lib/libobjc-trampolines.dylib, 0x0106): tried: '/usr/lib/libobjc-trampolines.dylib' (no such file, not in dyld cache)`: the file only exists in the OS cryptex and dyld does not search cryptexes at ignition_level=1. 19 cryptex files have no root counterpart (OS: DriverKit dyld cache + .symbols, VisualTestKit, libffi-trampolines, libobjc-trampolines, libstdc++ 6.0.9 + 2 symlinks; App: SafariSupport.bundle, AuthenticationServicesAgent, com.apple.Safari.History, passwordbreachd, safarifetcherd, webbookmarksd, webinspectord, webpushd) -> hard-linked (symlinks copied: Linux hfsplus cannot hard-link symlinks); `tools/mkiosroot.sh` does it generically. watchdogd still crashed: the `-wdt_no_pmu` / `/arm-io/wdt no-pmu` check (0x10000a01c) only runs when os_variant_has_internal_diagnostics; otherwise 0x100009b18 looks up IOServiceMatching("IOService") + IOPropertyMatch {IOPMUPrimary: 1} (NSConstantDictionary) and crashes if absent, then IORegistryEntrySetCFProperty(boot stage 0xff) (failure only logged). Patch **0045**: IOWatchdog sets IOPMUPrimary = 1 and accepts setProperties; CI `38961814194` -> `xnu/out-0045`. **Image v6** = v5 + the 19 cryptex links, SHA256 7c610c6a...297a, `ios16-root-20H392-v6.simg` (full flash needed). Boot image `shim/out/shim-boot-0045n.img` (0045 + sweet-0044hi.adt). Still open: SpringBoard (no display), backboardd (uncaught exception, reason none), mediaserverd (Fig NULL dict), logd (main+0x18160), lockdownd (main+0x4c1b4).
- **System survives daemon crashes (boot 0043p, video frames VID_20261016_215006 00:25/00:42/00:54).** With the v5 plist patch no panic: launchd keeps launching (runningboardd, powerd, tccd, healthd, peopled, cryptexd, routined, aksd, mediaremoted, familynotificationd, nanoprefsyncd, navd, cloudpaird, passd, amsaccountsd, accessoryupdaterd, wcd ...). Shared cache slide that boot 0x406b4000 (`research/crash-0043p.txt`). watchdogd still crashed at main+0xb82c from the PMU step: `-wdt_no_pmu` is only honored when os_variant_has_internal_diagnostics; customer builds read `no-pmu` from IODeviceTree:/arm-io/wdt (IORegistryEntrySearchCFProperty) -> mkadt adds /arm-io/wdt {no-pmu}. **cryptexd** sig 6: abort_with_payload <- os_crash_fmt <- `__darwin_arm_secure_boot_chip.cold.7` ("panic: failed to get crypto algorithm") <- `_img4_chip_select_personalized_ap`: reads /chosen use-ddi-secure-boot, allow-ecid-mismatch, crypto-hash-method ("sha1" | "sha2-384"), /chosen/asmb lp-smb0/1 -> mkadt adds crypto-hash-method "sha2-384". **identityservicesd** sig 6 = same `_objc_fatal` in imp_implementationWithBlock as CommCenter (block trampolines; message needed). lockdownd sig 5 at main+0x4c1b4. Patch **0044**: crash report adds `CRASH name reason ns N code X: text` (exit reason description = abort_with_payload text). CI `38959096811` -> `xnu/out-0044`, QEMU OK. Image `shim/out/shim-boot-0044n.img` = 0044 + `shim/out/sweet-0044hi.adt` (wdt no-pmu, crypto-hash-method) + cmdline (wdt=-1, no -wdt_no_pmu).
- **First symbolicated daemon crashes (boot 0043u on journaled v4, 19:38:53 UTC; video frame chat image 36; transcribed in `research/crash-0043u.txt`; shared cache slide 0x40d60000 for every process).** (Gemini's video transcription invented values, use frames only.) `tools/sweet-crash.py research/dsc/dyld_shared_cache_arm64 x research/crash-0043u.txt`:
  - **watchdogd** pc main+0xb82c (`_os_crash` trap helper) from 0x100009bb0 = set PMU boot stage via IORegistry (no PMU) -> fix: boot-args `wdt=-1 -wdt_no_pmu` (Apple's own test mode writes "%s wdt=-1 -wdt_no_pmu"; watchdogd logs "skipping setting PMU boot stage because -wdt_no_pmu found in boot-args or no-pmu found in EDT"). Kernel effect of wdt=-1: sched hygiene debug off, zone_exhausted_timeout -1 (harmless).
  - **SpringBoard** sig 5 in `-[FBSDisplayMonitor _initWithBookendObserver:transformer:]+1160` <- `+[FBDisplayManager sharedInstance]` <- `FBSystemShellInitialize` <- `SBSystemAppMain`: no main display. GUI WALL: display service (IOMobileFramebuffer / CoreAnimation display).
  - **backboardd** sig 6: abort <- std::terminate <- _objc_terminate (uncaught exception; message not shown).
  - **CommCenter** sig 6: `_objc_fatal` in `imp_implementationWithBlock` (from `+[_PFRoutines initialize]`, CoreData): objc block trampolines cannot be allocated -> likely kernel gap (trampoline page remap); will hit many processes.
  - **mediaserverd** sig 11: CFDictionaryGetValue(NULL) in FigNotificationListenerStartNotifications (routing session manager).
  - **logd** sig 5 at its main+0x18160 (bt main+0x9dfc); lockdownd sig 5 (not yet symbolicated).
  - Image v5 = v4 + launchd.plist (`/System/Library/xpc/launchd.plist`, contiguous at image offset 0x1e025f000, 1456980 bytes) with the 7 `_PanicOnCrash` keys renamed `_PanicOnCrasX` in place (raw image patch, no metadata change) -> `diff-v4-v5.simg` = 7 blocks; v5 SHA256 78f96974...185a. Boot image `shim/out/shim-boot-0043p.img` = 0043 + `wdt=-1 -wdt_no_pmu`.
- **0043 console crash reports (QEMU verified); image `shim/out/shim-boot-0043u.img`; NEXT: flash v4 + boot 0043u.** Boot 0042u (video photo IMG_20261016_211646): short exec lines work; watchdogd crashes with NO `SWEET WDT:` line (fails before its first IOWatchdog call: lookup or IOServiceOpen, or elsewhere); lockdownd, SpringBoard, mediaserverd, CommCenter, identityservicesd, backboardd, watchdogd all crash repeatedly ("Too many corpses being created" = corpse limit, no crash reports). AKS callers: lockdownd (0, 7), imagent/sleepd (0, 17, 7), backboardd (0, 17), UserEventAgent (0, 7, 102, 103 -> unsupported). Patch **0043**: `sweet_crash_log` (osfmk/kern/exception.c, called from proc_prepareexit for crash exits on the current proc) prints `CRASH name[pid] sig S code (exc<<32|code) sub X slide SR mh MH`, `CRASH name pc lr sp fp far`, `CRASH name bt` (10 fp frames); AKS sel 103 notify_state_is_set -> 0. CI `38954520419` -> `xnu/out-0043`. QEMU test: init forks, child `brk #1`: `CRASH launchd[2] sig 5 code 0x600000001 sub 0x1000004f0 slide 0x0`, pc correct, parent keeps running (pid 1 crashes panic before proc_prepareexit, so test with a child). `tools/sweet-crash.py CACHE ROOT [report]` symbolicates typed-in CRASH lines (`ipsw dyld a2s` with addr - slide; main binary offsets = addr - mh + 0x100000000).
- **v3 reflashed 18:50-18:55 UTC (`research/flash-userdata-v3b.log`, 24/24 OKAY); 0042u booted 19:01:17 UTC on the clean disk (`research/boot0042u-2.log`), SLOW MO VID pending (watchdogd failure reason).** ABL hangs on the first `fastboot boot` after every big flash; user re-enters fastboot. User gave standing approval to reflash/boot without asking (memory standing-flash-approval). Commits now end with `Co-Authored-By: dipa-studio <183933817+dipa-studio@users.noreply.github.com>` (user request; never a Claude trailer).
  - watchdogd flow: connect_to_kext (0x100005408): IOServiceGetMatchingService("IOWatchdog") fails -> "failed to discover" + _os_crash; IOServiceOpen(type 1) fails -> "IOServiceOpen failed with error : 0x%x" + _os_crash. Then sel 0 (out 1 scalar): 0 -> "userspace monitoring disabled", 0x100009bb0, sleep 2, exit(0); nonzero -> "userspace monitoring enabled", normal operation.
  - **Image v4 (JOURNALED):** `tools/mkiosroot.sh` now does `mkfs.hfsplus -J`, mounts with `-o loop,force`, adds the /System/Library/Caches/com.apple.dyld hard links (44) and /private/var/keybags/systembag.kb (0700/0600 root). Built from the IPSW dmgs (OS 087-86698-044, SystemOS 087-86593-047, AppOS 087-86715-047) -> `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v4.img` SHA256 fba4aeae...ec0f9 + `.simg`. Header: attr 0x80002100 (unmounted, journaled), journal info block 129, journal 8 MiB at 532480 flagged need-init. Linux `fsck.hfsplus -n` reports "Volume header needs minor repair" on any fresh journaled volume even after -y (quirk). QEMU: 64 MB journaled test root (`rd=md0 rootrw=1`, 0042 kernel): `hfs: Initializing the journal (joffset 0x3000 sz 0x80000)` -> mounted -> init runs, no panic. NEXT: flash v4 (full), boot 0042u.
- **SPRINGBOARD STARTS (boot 0041u 18:39:53 UTC, photo IMG_20261016_204142).** backboardd, bluetoothd, fairplayd.H2, SpringBoard (pid 111, crashed: "Too many corpses"), xpcproxy run; `SWEET AKS: backboardd sel 0 -> 0`, `sel 17 in 1/0 out 0/4096 -> 0`. Panic: watchdogd[113] exited, namespace 2 subcode 5 (SIGTRAP = its _os_crash): watchdogd crashes on purpose when IOServiceGetMatchingService("IOWatchdog") fails ("connect_to_kext() unexpectedly failed") or IOServiceOpen(type 1) fails ("IOServiceOpen failed with error"); IOWatchdog DOES register (QEMU msgbuf: `SWEET WDT: IOWatchdog registered`), cause not on screen yet -> need video. Note `wdt_test=no_user_checkin` makes watchdogd log "detected testing boot-args to not run, exiting" (path not reached here?). **HFS CORRUPTION:** `hfs_swap_BTNode: offset #0 invalid (0xBC1D) (blockSize 0x2000 numRecords 81)`, `node=19677 fileID=4` (catalog), `hfs_mark_inconsistent` repeated. Likely cause: non-journaled HFS + unclean panics while writing (0038e wrote a 2.7 GB core then panicked; every boot since ends in panic). Plan: reflash v3 (clean), longer term HFS journaling. Patch **0042**: one short `exec NAME` console line instead of the 3-line "Allowing spawn of iOS binary" notice; CI `38949451276` -> `xnu/out-0042`; image `shim/out/shim-boot-0042u.img` (same cmdline as 0041u).
- **LAUNCHD DAEMONS RUN (boot 0039u, 18:29:13 UTC, photo IMG_20261016_203052).** With `launchd_unsecure_cache=1` all boot tasks pass and launchd starts LaunchDaemons (pids > 100: identityservicesd 114 and lockdownd 106 crash, "Too many corpses being created"). Fatal: `watchdogd: ERROR: failed to discover watchdog KEXT service` -> exit (namespace 2 = signal, subcode 5) -> `_PanicOnCrash` panic. watchdogd: IOServiceMatching("IOWatchdog"), IOServiceOpen type 1, IOConnectCallScalarMethod sel 0/1 (check in)/3/4 (disable/enable userspace monitoring)/5, IOConnectCallStructMethod sel 2 (1 KiB in) and 9 (small out); VM path ("detected virtual machine environment and no watchdog KEXT found, exiting...") only when it detects a VM. Console also flooded with `hfs: set hardlink bit on vol=iOSRoot cnid=321318` (Linux-made hard links lack kHFSHasLinkChainBit; repaired only in memory on each lookup). Patches **0040** (`iokit/Kernel/arm/SweetWatchdog.cpp`: IOWatchdog + IOWatchdogUserClient, all selectors succeed with zeroed outputs, each logged once as `SWEET WDT: ...`) and **0041** (sweet_hfs hfs_catalog.c: the three link-repair printfs limited to 8 per boot); CI `38948245372` PASSED -> `xnu/out-0041` (QEMU regression OK). Image `shim/out/shim-boot-0041u.img` (0041 + sweet-0038hi.adt + ufsrw rootrw + launchd_unsecure_cache=1) booted 18:39:53 UTC (`research/boot0041u.log`), photo pending.
- **v3 FLASHED + keybag passes; next wall AMFI (launchd_cache_loader).** User said "flash, nothing to lose": `fastboot -S 512M flash userdata ios16-root-20H392-v3.simg` 17:51:52-17:57:01 UTC, 24/24 chunks OKAY (`research/flash-userdata-v3.log`). userdata = v3 (SHA256 fa84274c...22df) until the next rw boot. ABL fastboot then hung ("Sending" forever, `reboot bootloader` timed out) until the user re-entered fastboot by hand (common Xiaomi ABL crash). Boot 0038e 18:06:15 UTC (`research/boot0038e-3.log`), photos IMG_20261016_200812/200838: keybag OK, then boot tasks tzinit (`Bad zoneinfo link`, resets to system partition, OK), finish-restore, finish-demo-restore, sysstatuscheck, prng_seedctl (`failed to load kernel prng seed`, `failed to load virtual random (-147)`, AllowCrash), auearlyboot, then **launchd_cache_loader: `abort: Unable to find AMFI`** -> 2.7 GB core written to /cores in 91 s -> launchd panic `launchd_cache_loader[22] exited -- exit reason namespace 7 subcode 0x1`. launchd_cache_loader: SecStaticCodeCheckValidity of /System/Library/xpc/launchd.plist with detached .sig, then IOServiceMatching("AppleMobileFileIntegrity") + IOServiceOpen type 0 + IOConnectCallMethod sel 6 (1 scalar = 1, struct in = cdhash) = trust the cdhash, then sends the cache to launchd. Boot-arg `launchd_unsecure_cache=1` skips signature + AMFI ("Using unsecure cache"); the value is parsed with strtoul base 10 (helper 0x100002da8), so a path = 0 = off: boot 0039w (18:17:13 UTC, `=/System/Library/xpc/launchd.plist`, photo IMG_20261016_202008) still aborted `Unable to find AMFI` (core dump now skipped: `Failed to generate core file ... error 14`, so 0039 works). Image `shim/out/shim-boot-0039u.img` uses `launchd_unsecure_cache=1`. Patch **0039**: do_coredump = 0 on SWEET (kern.coredump sysctl still works); CI `38945267341` -> `xnu/out-0039` (_do_coredump now in __common). Image `shim/out/shim-boot-0039w.img` = 0039 + sweet-0038hi.adt + ufsrw rootrw + `launchd_unsecure_cache=/System/Library/xpc/launchd.plist`. TODO later: real AppleMobileFileIntegrity IOService (sel 6 trust cdhash etc.).
- **Image v3 ready, awaiting user "flash".** Boot 0038w 17:38:10 UTC, photo (chat image 30): `SWEET AKS: keybagd sel 0 -> 0x0`, `sel 14 out 1/0 -> 0x0` (x2), `MKB_INIT: mount is with NO CPROTECT ... does not support Data Protection`, `System Keybag loaded`; then keybagd still opens /private/var/keybags/systembag.kb (0x100012d3c, for BackupKeyBagKeys) -> ENOENT, `no-effaceable-storage` not set -> `Can't load the keybag. tears in rain... Time to die` -> `FATAL KEYBAG ERROR: kb_load` -> reboot -> Halt/Restart Timed Out panic. Fix: ADT `/defaults/no-effaceable-storage` (mkadt.py; `shim/out/sweet-0038hi.adt`, `research/q0038-hi.adt`, QEMU OK) + file `/private/var/keybags/systembag.kb` (0600 root, dir 0700; XML plist {KeyBagKeys: data}). **v3 image** `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392-v3.img` = v2 + that file, fsck OK, SHA256 fa84274c...22df; `ios16-root-20H392-v3.simg` (img2simg: zero blocks become FILL chunks, so a full flash overwrites every block the rw boots changed). Flash: `fastboot -S 512M flash userdata ios16-root-20H392-v3.simg`, then boot `shim/out/shim-boot-0038e.img` (0038 + sweet-0038hi.adt + ufsrw=1 rootrw=1).
- **0038 AppleKeyStore (CI 38938744130 PASSED -> `xnu/out-0038`; QEMU msgbuf: `SWEET AKS: AppleKeyStore registered (no passcode, unlocked)`; phone image `shim/out/shim-boot-0038w.img` booted 17:38:10 UTC, `research/boot0038w.log`, photo pending).** QEMU full kernel log trick (fb.txt is only the last screen): `llvm-nm KERNEL | grep _msgbufp` -> `--hmp "x /1gx ADDR"` gives struct msgbuf address (static, 0xfffffe0007b56678 on 0038) -> `--hmp "x /3gx STRUCT"` gives bufc (3rd quad) -> `--hmp "memsave BUFC 0x20000 \"FILE\""` (quote the file name), `tr -d "\0" < FILE`. Same inputs give the same addresses. Boot 0037w 17:08:35 UTC, photo IMG_20261016_191052: bindfs fixed mount; mount-phase-1/2 pass; `cc_fips_test` boot task: all corecrypto FIPS POST tests PASSED in userspace; then keybag boot task `keybagd --init`: `MKB_INIT: FATAL KEYBAG ERROR: failed to load system bag`, `Could not get options entry from the device tree` (x2, IODeviceTree:/options = NVRAM, non-fatal), `REBOOTING INTO RECOVERY MODE` -> reboot -> panic `Halt/Restart Timed Out @IOPlatformExpert.cpp:884` (no restart path on SWEET).
  - keybagd (static libaks): load_devicebag (0x100012c30) calls aks_get_system (sel 14, 0-1 scalar in, 1 out) and loads /private/var/keybags/systembag.kb only if the result is 0xe00002f0 (NotFound); any other error -> fatal; SUCCESS -> skip the file (userspace-reboot path). systembag.kb is a plist {KeyBagKeys}; decrypted with effaceable storage unless IODeviceTree:/defaults has `no-effaceable-storage`. Image has no /private/var/keybags. has_data_protection: statfs of /private/var; no cprotect on our HFS -> device-bag volume mapping skipped.
  - libaks connection: IORegistryEntryFromPath / IOServiceMatching("AppleKeyStore"), IOServiceOpen type 0, then selector 0 (no args) must succeed.
  - **AppleKeyStore selector map (iOS 16.7 AppleKeyStore.framework, via `ipsw dyld disass`):** 0 open, 2 create_bag, 3 save_bag, 4 unload_bag, 5 set_system_with_passcode, 6 load_bag, 7 get_lock_state (1 in, 1 out; bit 2 = unlocked since boot), 9 unlock_device, 10 wrap_key, 11 unwrap_key, 12 unlock_bag/test_stashed_kek, 13 lock_bag, 14 get_system, 15 change_secret_se, 16 internal_state, 17 get_device_state (1 in, struct out <=4K: DER SET of SEQUENCE{UTF8 key, INTEGER}: ss state, sls lock_state (MKBGetDeviceLockState returns it), sb backoff, sbo, sfa failed attempts, sfao, sgs generation, sas assertions, sgpe grace, srcd countdown, sr reserved), 18 recover_with_escrow_bag, 19 obliterate_class_d, 20 drain_backup_keys, 21/22 set/clear_backup_bag, 23 get_bag_uuid, 24 rewrap_key_for_backup, 26/27 assert_hold/drop, 30/31 set/get_configuration, 33-37 stash, 38 auth_token_create, 40 generation, 41 fdr_hmac_data, 42 verify_password, 43 _aks_operation, 44 remote_session, 49/50 signing keys, 51/53/54 stash enable/persist/escrow, 55 unload_session_bags, 57/58 remote peer, 59-62 smartcard, 63 migrate_s_key, 65 set_keybag_for_volume_with_cookie_persona, 66/67 sync bags, 68 copy_volume_cookie_persona, 69-73 se_*, 74-79/84/85/99 fv_* (FileVault/VEK), 80/81 absinthe, 82 make_public_backup_bag, 83 drop_auxiliary_auth, 86 kext get/set options, 87 invalidate_bag, 88-91/94/95/106/107 se_*, 92 lower_iteration_count, 93 get_internal_info, 96-98/117 keybag_persona create/list/delete, 100/101 assert promote/consume, 102/103 notify_state set/is_set, 104 fairplay, 105 change_secret_epilogue, 108 delete_xart_leak, 110-113 backup volume, 114 sealed_hashes_set, 115 prewarm_sps, 118-128 AKSIdentity*/AKSGetLastUser.
  - Patch **0038**: `iokit/Kernel/arm/SweetKeyStore.cpp` (class AppleKeyStore on IOResources, IOUserClientClass AppleKeyStoreUserClient; personality in KernelConfigTables.cpp): 0/4/5/9/12/13/26/27/55/102 success, 14 -> handle 0, 7 -> 0x4, 17 -> DER state (ss 4, sls 0), others kIOReturnUnsupported; logs `SWEET AKS: <proc> sel N arg0 in a/b out c/d -> ret` for every call.
  - Tools: shared cache copied to `research/dsc/` (git-ignored; ipsw needs a writable dir for its .a2s symbol cache); `ipsw dyld disass research/dsc/dyld_shared_cache_arm64 --image AppleKeyStore|MobileKeyBag`; scratchpad `annot2.py BIN DIS FIXUPS` annotates llvm-objdump output with strings and import names (`ipsw macho info --fixups`).
- **0037 bindfs (phone boot 0037w pending photo).** Boot 0036mw 16:57:47 UTC, photo IMG_20261016_190102: WRITABLE ROOT WORKS: `/dev/ufsroot0 on / (hfs, local, noatime)` after mount_hfs update (GetMasterBlock EBUSY harmless), coredump of mount written to /cores (rw). Then `mount: failed to mount /private/preboot/5CBE3601.../usr/standalone/firmware -> ...: Operation not supported by device (19)` -> exit 74 -> launchd panic `mount[2] exited`. Cause: the macOS-derived config had nullfs but no bindfs (ENODEV = unknown fs type). Caller passes env 0 -> bind not required, ENOENT (missing source dir) tolerated. Patch **0037** adds `bindfs` to FILESYS_BASE in MASTER.arm64.sweet; CI `38936550483` -> `xnu/out-0037` (bindfs_vfsops present; QEMU probe regression OK). Image `shim/out/shim-boot-0037w.img` (0037 + sweet-0036hi.adt + ufsrw=1 rootrw=1) booted 17:08:35 UTC (`research/boot0037w.log`). NOTE: userdata has now been modified by rw boots; it no longer equals the v2 image, so sparsediff with v2 as OLD is not safe any more (full reflash or a new baseline needed).
- **0036 phone: launchd boot tasks run; mount -P 1 needs /chosen/boot-manifest-hash.** Boot 0036hr 16:15:32 UTC, photo IMG_20261016_185236: platform check passes; fsck boot task runs (`fsck: not booting main or diagnostic OS. Skipping fsck on OS container`); mount-phase-1: `failed to get filesystem info` x2 (no IODeviceTree:/filesystems/fstab, non-fatal), `mount: data volume missing, but not required in env: 0`, mount_hfs update of /: `GetMasterBlock: Error 16 opening /dev/ufsroot0` (EBUSY, mounted block dev, non-fatal) then mount() EROFS (read-only boot) -> `mount: / failed with 1`; then `failed to get boot-manifest-hash` -> errx exit 66 -> launchd panic `mount[3] exited`. /sbin/mount: fn 0x100005f90 reads IODeviceTree:/chosen boot-manifest-hash (hex), bindfs-mounts /private/preboot/<HEX>/usr/standalone/firmware (ENOENT tolerated unless required). `tools/mkadt.py` now adds `boot-manifest-hash` = SHA-384("sweet boot manifest") (5CBE3601...5205). ADTs `shim/out/sweet-0036hi.adt`, `research/q0036-hi.adt` (QEMU probe OK, csops 0x26000001). Image `shim/out/shim-boot-0036mw.img` (0036 + new ADT + ufsrw=1 rootrw=1), booted 16:57:47 UTC (`research/boot0036mw.log`): FIRST WRITABLE BOOT of userdata (restore source: v2 image on SSD).
- **0036 CS_SIGNED (phone boot 0036hr pending photo).** Phone boot 0035hr 2026-10-16 15:57:02 UTC (`research/boot0035hr.log`, photo IMG_20261016_180249): highmem works on hardware; shared cache maps (trust-cache warnings only), Apple launchd runs (`Darwin Bootstrapper Version 7.0.0`, `entering ondemand mode`), starts boot task fsck, then userspace panic `boot task is not a platform binary (fsck, /sbin/fsck)`. launchd+0x3fea0: `csops(pid, CS_OPS_STATUS)` must have bit 0x04000000. Cause: nothing set CS_SIGNED (AMFI does it in vnode_check_signature), so csproc_get_blob() returns NULL for every process and csops never reports CS_PLATFORM_BINARY (also breaks per-process cdhash/entitlement queries). QEMU probe init (`csops` status printer, scratchpad csq/) on 0035: `0x02000001`; patch **0036** (`*cs_flags |= CS_SIGNED` in the SWEET AMFI hook), CI `38929635791` -> `xnu/out-0036`: `0x26000001`. Images `shim/out/shim-boot-0036hr.img` / `-0036hw.img` (same cmdlines as 0035). 0036hr booted 16:15:32 UTC (`research/boot0036hr.log`). Next launchd boot tasks (embedded __config plist): fsck -qL and mount -P 1/2 (RequireSuccess); no /etc/fstab on the image.
- **0035 ENTITLEMENTS + PLATFORM BINARIES (QEMU verified).** `xnu/patches/0035-sweet-amfi-entitlements-platform-binaries.patch`: new `iokit/Kernel/arm/SweetEntitlements.cpp` (OSObject subclass: OSDictionary from OSUnserializeXML of the signature's XML entitlements + original XML for CS_OPS_ENTITLEMENTS_BLOB); `bsd/kern/sweet_amfi.c` answers queryEntitlementBoolean/String(+WithProc, string also matches array members), copyEntitlementAsOSObject(+WithProc, retained), asdict (retained), get_xml (kalloc_data copy); built-in MAC policy "SWEET AMFI" (registered from bsd_init before vfs/exec) with mpo_vnode_check_signature: every signed binary on the MNT_ROOTFS mount gets CS_PLATFORM_BINARY (stand-in for AMFI's static trust cache: iOS system binaries are ad-hoc, cd_flags 0x2, platform byte 0), and attaches entitlements ONLY for the writable copy (cs_blob != csb_ro_addr; revalidation passes the RO blob). LESSON: first version attached in adjustContextWithoutMonitor -> QEMU `unable to adjust OSEntitlements context without monitor: 53` because ubc_cs_convert_to_multilevel_hash (4K-page signatures on 16K kernel) sets csb_entitlements_blob = NULL; ALL iOS 16 binaries (launchd, backboardd, SpringBoard) use 4K code pages, so this would have broken launchd on the phone. Boot-arg `sweet_amfi_debug=1` logs `SWEET AMFI: <ident>: N entitlements (test: ...)`. `tools/mkinit.py OBJ OUT [ENTS.plist]` adds an XML entitlements blob (special slot -5; default output byte-identical). QEMU `research/q0035-ent` (RAM disk `research/root-hfs-ent.img`, init with 3 test entitlements, highmem): `org.sweet.init: 3 entitlements (test: bool 1 string 1 array 1)` then init PID 1. MAC policies loaded: 0034 = 1, 0035 = 2 (QMP `mac_policy_list`). CI `38909807230` -> `xnu/out-0035`. Phone images: `shim/out/shim-boot-0035hr.img` (highmem, ro), `-0035hw.img` (highmem, +ufsrw=1 rootrw=1), `-0035r.img` (low mem fallback).
- **PHONE TEST ORDER (updated):** 0035hr -> if instant reset, 0035r -> then 0035hw.

- **HIGHMEM: XNU in the upper DDR bank, ~3.95 GiB (was 1.2 GiB).** Live FDT memory = <0x80000000 2G> + <0x1_00000000 0xfcc00000>; all fixed reserved-memory carve-outs are in the low bank (highest: ramdump_fb 0xaf000000, rmtfs ~0xfde01000), dynamic pools have alloc-ranges <= 0xffffffff, UFS node has no `iommus` (Linux DMAs anywhere). `tools/mkadt.py --highmem` sets /chosen dram-base 0x100000000 dram-size 0xfcc00000; `tools/mkpayload.py` now takes phys_base/mem_size from the ADT (default payloads byte-identical, verified); `tools/qemu-sweet.py` sizes -m from the payload (8G for highmem). Patch **0034** (`0034-sweet-ufs-dma-above-4g.patch`): UFS refuses DMA memory above 4G without UFSHCI CAP.64AS (bit 24), logs `DMA at 0x...`. CI `38905889883` success -> `xnu/out-0034`. **QEMU verified** (`research/q0034-hi`, ADT `research/q0034-hi.adt` --qemu --highmem, HFS RAM disk): phys_base 0x100000000, init PID 1 heartbeat, no panic; `max_mem` = `mem_size` = 0xfcc00000 read via QMP (`research/q0034-hi-mem`). Phone images: `shim/out/shim-boot-0034hr.img` (read-only) / `-0034hw.img` (+ufsrw=1 rootrw=1), ADT `shim/out/sweet-0034hi.adt` (asmb SIP + sweet-ufs + highmem). PHONE RISK: unknown runtime TZ/XPU protection in the upper bank would show as an instant reset; fall back to 0033r/w. Watchdog ruled out for the overnight reset: msm-watchdog bark is 20 s (qcom,bark-time 0x4e20) yet panic screens stay up for many minutes.

- **Offline research 2026-10-16 (user away, phone unavailable). Images rebuilt** `shim/out/shim-boot-0033r.img` / `-0033w.img` now also carry `wdt_test=no_user_checkin`. NEXT ON PHONE: boot 0033r (expect past in6_nigroup), then 0033w (first writes: rw root fixes HFS link-chain bits itself and gives /var for launchd sockets /var/run/syslog, /var/run/lockdown.sock).
- **launchd.plist** (`/System/Library/xpc/launchd.plist`, compiled; 532 daemons, 66 RunAtLoad/KeepAlive, list in scratchpad `early-daemons.txt`). `_PanicOnCrash` daemons: containermanagerd/usermanagerd (InternalOnly), lockdownd/remoted/trustd/remotepairingdeviced (AutomationOnly), **watchdogd unconditional** (KeepAlive SuccessfulExit=false). watchdogd needs IOKit `IOWatchdog` kext service; if absent it exits only when `kern.hv_vmm_present` (ADT `/defaults/vmm-present`, hvg_sysctl.c) or with boot-arg `wdt_test=no_user_checkin` ("detected testing boot-args to not run, exiting"; reads kern.bootargs). Using the boot-arg (vmm-present would change many daemons; keep as an option).
- **GUI path research (QuartzCore in the shared cache, `ipsw dyld str CACHE STR`, ~10 s/search):** CA::WindowServer has IOMFBServer/IOMFBDisplay (Apple DCP/IOMobileFramebuffer), AppleCLCDDisplay, AppleRGBOutDisplay, **VirtualServer/VirtualDisplay** and **MemorySurface**, EAGLServer. Renderers: CA::OGL::MetalContext, GLContext and a **CPU software rasterizer CA::OGL::SW** (samplers/blend modes; "image is too large for software renderer"). Env knobs: `CA_ENABLE_OGL`, `CA_DISPLAY_TYPE`, `CA_FORCE_LOCAL_SERVER` (249 CA_* vars). Hypothesis (unverified): backboardd's render server could run a VirtualServer/MemorySurface with the SW renderer and we copy the surface to the ABL framebuffer -> needs disassembly of CA::WindowServer server creation. Expected other GUI walls: IOSurface (closed-source kext; needs a reimplementation), IOMobileFramebuffer user client, IOHID touch events, AppleKeyStore/keybag (SEP) for lock state, MobileGestalt answers (AppleCLCD etc.).

- **0032r RESULT (boot 05:07:50 UTC, `research/boot0032r.log`, photo IMG_20261016_071200): SHARED CACHE MAPPED, LAUNCHD RUNS.** With SIP relaxed (ADT asmb) the `not in trust cache` lines are only warnings for every subcache (.31 ... .42.dyldlinkedit); no SIP rejection; libSystem loads; launchd configures networking (`skip attaching fsw to lo0 using legacy TX model`) -> kernel data abort far 0x28 in `SHA1Init` (symbolized as sha1_init) from `in6_nigroup` (IPv6 on lo0): g_crypto_funcs NULL. Kernel addresses are unslid: `~/swift/usr/bin/llvm-symbolizer --obj=KERNEL ADDR` works directly.
- **0032** BearSSL SHA-1 (`0032-sweet-bearssl-sha1.patch`), **0033** BearSSL SHA-384/512 (`0033-sweet-bearssl-sha384-sha512.patch`, BearSSL ctx stored in SHA512_CTX, static-asserted to fit; ubc_subr.c uses SHA-384 for code directories). `0020-stage-bearssl.sh` now also stages hash/sha1.c, hash/sha2big.c, codec/{dec,enc}{32,64}be.c. CI `38854909053` success (0032-only run `38854802073` superseded). Kernel `xnu/out-0033` (SHA1Init -> _br_sha1_init verified by disassembly; nm symbols have a leading underscore). Images (ADT sweet-0032): `shim/out/shim-boot-0033r.img` (read-only), `shim-boot-0033w.img` (+ufsrw=1 rootrw=1). Still NULL g_crypto_funcs users: des, chacha20poly1305, rsa, aesxts, rand, flow_divert.
- `research/check-patch-order.py` now skips patches touching CI-staged sweet_hfs sources (0031) instead of stopping.

- **0031r RESULT (boot 05:03:52 UTC 2026-10-16, `research/boot0031r.log`, photo IMG_20261016_070508): SHARED CACHE FOUND.** dyld now opens `dyld_shared_cache_arm64` (v2 image hard links), kernel refuses to map it: `shared_region: ... map(...'dyld_shared_cache_arm64'): not in trust cache` -> `vnode is not SIP-protected` (vm_unix.c: macOS arm64 path needs trust cache, else root-owned + SF_RESTRICTED unless CSR_ALLOW_UNRESTRICTED_FS) -> `dyld cache '(null)' not loaded: syscall to map cache into shared region failed` -> libSystem not loaded -> coredump to /cores fails error 30 (EROFS) -> panic initproc failed -> "MACH Reboot" (spins; screen kept). Lots of `hfs: set hardlink bit on vol=iOSRoot cnid=... linkid=...` spam (Linux-made hard links lack the link-chain bit; read-only mount cannot persist the fix). FIX: ADT `/chosen/asmb` with `lp-sip0`=0x6f (u64; incl. CSR_ALLOW_UNRESTRICTED_FS) and `lp-sip1`=1 (unauthenticated root), `tools/mkadt.py` -> `shim/out/sweet-0032.adt`. Images with kernel 0031: `shim/out/shim-boot-0032r.img` (read-only) and `shim-boot-0032w.img` (+ufsrw=1 rootrw=1).

- **2026-10-16 morning:** userdata now = **v2** image (`diff-v1-v2.simg` flashed 21:06:50 UTC 2026-10-15, OKAY, `research/flash-userdata-diff-v2.log`); future diffs use `ios16-root-20H392-v2.img` as OLD. Booted `shim-boot-0029k.img` (ignition_level=1) right after (`research/boot0029k-v2.log`); **no photo** (user asleep). Next morning the phone was in PixelOS recovery = some hardware reset (ABL then booted Android, which cannot use userdata). OBSERVATION: our kernel cannot reset the SoC itself (`PE_halt_restart` is never set; reboot/halt print "MACH Reboot"/"CPU halted" and spin), so the reset came from outside XNU: Qualcomm watchdog or a TZ/XPU violation. Unknown which; re-boot 0029k and watch/video until the reset. Do NOT tap factory reset/format data in recovery (would wipe the iOS image).
- **0030** (`xnu/patches/0030-sweet-ufs-writes.patch`, CI `38852655542`): UFS WRITE(10) + read-modify-write for partial 4 KiB blocks + SYNCHRONIZE CACHE (DKIOCSYNCHRONIZE), only with boot-arg `ufsrw=1`; `ufs_write_blocks` re-checks partition bounds before every write (only place data goes out).
- **0031** (`xnu/patches/0031-sweet-read-write-root.patch`, CI `38852837176`): boot-arg `rootrw=1` clears MNT_RDONLY before the root mount (no data volume on sweet: /private/var is on the root; image has no /etc/fstab); Apple HFS accepts a dirty non-journaled ROOT read-write on SWEET (logs "mounting dirty non-journaled root read-write"). Later: journaled HFS for crash safety. Kernel `xnu/out-0031` (CI `38852837176` success, 5m31s; 0030 run cancelled by the newer push). Boot images (phone ADT `sweet-0028.adt`, ramdisk byte-matched): `shim/out/shim-boot-0031r.img` = 0029k args (read-only, ignition_level=1) and `shim/out/shim-boot-0031w.img` = same + `ufsrw=1 rootrw=1`. PLAN: boot 0031r first and film until the reset (diagnose overnight reset), then 0031w (first ever writes to userdata; v2 image on the SSD is the restore source). `research/check-patch-order.py` cannot check 0031 (staged sweet_hfs files are not in its index); CI stage scripts run first, so CI is the check.

- **ignition_level=1 result** (boot 21:01:39 UTC, `research/boot0029k.log`, photo IMG_20261015_230308): preboot/cryptex stages skipped (need >= 2/3), `dylib_cache: finding shared cache on system volume: ignition level = 1, search root fd = 4, subpath = System/Library/Caches/com.apple.dyld/` -> not found -> `ignition boot failed: 19` -> no shared cache -> libSystem not loaded. FIX IN PROGRESS: image v2 = copy of the flashed image + `/System/Library/Caches/com.apple.dyld/` with HARD LINKS to the 44 cryptex cache files (no extra space; avoids symlink-following doubts), then `tools/sparsediff.py ios16-root-20H392.img ios16-root-20H392-v2.img diff.simg` and flash only the diff. Boot with `ignition_level=1` (shim-boot-0029k.img).
- **ignition_level=0 result** (boot 20:58:07 UTC, `research/boot0029j.log`, photo IMG_20261015_230037): all stages skipped as "ignition level insufficient" (hello/preboot/cryptex1 sniff >= 2, select os cryptex path/graft >= 3, **dyld_cache >= 1**), `ignition boot failed: 89`, "ignition partially disabled", then same `Library not loaded: /usr/lib/libSystem.B.dylib (no such file, no dyld cache)`. => dyld gets the shared cache directory from ignition's dyld_cache stage. Next: `ignition_level=1` (`shim/out/shim-boot-0029k.img`) to skip preboot/cryptex stages but run dyld_cache; its log "finding shared cache on %s ... subpath = %s" will show where it searches. If it searches rootfs /System/Library/Caches/com.apple.dyld, add a symlink there (image rebuild; consider a diff-only sparse flash tool to avoid rewriting 12 GB).
- **APPLE dyld/launchd RUNNING (libignition) with `unentitled_ios_sim_launch=1`** (boot 20:53:24 UTC, `research/boot0029i.log`, photo IMG_20261015_225617): kernel prints "Allowing spawn of iOS binary ... boot-arg set to true"; `shared_region: ... check_np ... vm_shared_region_start_address() failed` (no region yet, normal); dyld's built-in **libignition** (Darwin Ignition Sequence 1.0.0, libignition-51~1974; prints rd=ufsroot0, hardware target sweet, ignition level 0x5, platform 0x2, boot spec local, 7 stages): `hello from launchd.1` -> `preboot: mounting preboot` -> `failed to lookup preboot: 25` (ENOTTY: looks for a separate Preboot volume by role) -> ignition fails -> dyld `Library not loaded: /usr/lib/libSystem.B.dylib ... (no such file, no dyld cache)` -> launchd exit -> panic initproc failed. dyld boot-args found via strings: `ignition_level`, `ignition_force_dylib_root` (forced rootfs/livefs/cryptex fallback), `ignition_halt_after`, `ignition_live_app_graft`; dyld cache search paths include /System/Cryptexes/OS, /System/Volumes/Preboot/Cryptexes/OS/, /private/preboot/Cryptexes/OS/ (present in our image). Next try `ignition_level=0` (`shim/out/shim-boot-0029j.img`).
- **APPLE launchd EXECUTED FROM UFS (2026-10-15).** Flashed iOS image: `img2simg` -> `ios16-root-20H392.simg` (12.1 GB) then `fastboot -S 512M flash userdata ...simg` (24 chunks, 307 s, `research/flash-userdata-ios16.log`; a raw-image flash was killed by earlyoom at 2.6 GB RSS - always flash the sparse image). ABL fastboot hung on getvar after the flash; power+vol-down fixed it. Boot 0029u 20:49:04 UTC (`research/boot0029u-ios16.log`), photo IMG_20261015_225134: `hfs: mounted iOSRoot` -> load_init_program /sbin/launchd -> panic `unexpected SIGKILL of launchd (CS_KILLED) with reason -- namespace 9 code 0xe` = OS_REASON_EXEC / EXEC_EXIT_REASON_WRONG_PLATFORM: our kernel is XNU_TARGET_OS_OSX, iOS-platform binaries are "alien" and refused unless spawn attr platform set or DEVELOPMENT TUNABLE `unentitled_ios_sim_launch`. Next try: same kernel, cmdline + `unentitled_ios_sim_launch=1` (`shim/out/shim-boot-0029i.img`). LONGER TERM: macOS-config kernel marks iOS maps alien (vm_map_mark_alien) and differs from iOS in many policies; an iPhoneOS-config kernel build may eventually be needed.
- **iOS 16 ROOT IMAGE BUILT:** `/run/media/dipa/android/ios-sweet-ipsw/ios16-root-20H392.img` (16 GiB case-sensitive HFS+, label iOSRoot, 12 GB used, fsck OK after `fsck.hfsplus -y` rewrote 32 non-ASCII localized WorkflowKit Shortcut names Linux decomposes differently). Built by `tools/mkiosroot.sh OS.dmg SYSTEMOS.dmg APPOS.dmg OUT.img [SIZE]` (needs sudo; apfs-fuse as root; OS dmg is UDIF -> link must end .dmg). Contents: iPhone X 20H392 OS root + SystemOS cryptex at /private/preboot/Cryptexes/OS (so /System/Cryptexes/OS -> cache, 44 files, dyld_v1 arm64) + AppOS at .../App. /sbin/launchd and /usr/lib/dyld root:wheel 0755. NEXT: (confirm) `fastboot flash userdata ios16-root-20H392.img` (auto-sparse), boot 0029u (rd=ufsroot0) -> Apple launchd attempt.
- **MILESTONE ON HARDWARE: BOOT FROM INTERNAL FLASH.** 0029 CI `38797924912` PASSED, `xnu/out-0029` SHA256 `a68bc847...2a37`. Image `shim/out/shim-boot-0029u.img` (payload-0029u = 0029 kernel + `shim/out/sweet-0028.adt` + `rd=ufsroot0`, NO RAM disk). Boots 20:01:34 and 20:12:37 UTC (`research/boot0029u*.log`), photo IMG_20261015_221403: `SWEET UFS: "userdata" on LUN 0: first block 0x2b8c00, 109643 MiB, read-only` -> `BSD root: ufsroot0, major 1, minor 0` -> `hfs: mounted SweetRoot on device b(1, 0)` -> `SWEET: root volume is not authenticated (error 25); continuing unsealed` -> `load_init_program ... /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` -> heartbeat (ran for minutes unattended). userdata currently holds the 8 MB `research/root-hfs.img`. NEXT: build a multi-GB HFS+ image with the iOS 16 (iPhone X 20H392) userland (OS rootfs + SystemOS cryptex contents incl. dyld shared cache at /System/Library/Caches/com.apple.dyld; preserve modes/owners/symlinks; decompress LZBITMAP via apfs-fuse), flash it to userdata (sparse, needs user OK), then try Apple's dyld/launchd. Driver is read-only (root mounts RDONLY), fine for first launchd attempts.
- **userdata FLASHED with the 8 MB HFS test volume** (user said "flash it", 2026-10-15 19:53:17 UTC, `research/flash-userdata-hfs-test.log`: `fastboot flash userdata research/root-hfs.img` OKAY; pre-checks: one sweet device, partition-size:userdata 0x1AC4BFB000, partition-type f2fs, image fsck OK, SHA256 3626ea69...). Android userdata is now destroyed (expected/approved). Boot 0028u at 19:53:20 (`research/boot0028u-hfs.log`), photo IMG_20261015_215541: `BSD root: ufsroot0` -> **`hfs: mounted SweetRoot on device b(1, 0)` from internal UFS** -> panic `rootvp not authenticated after mounting @bsd_init.c:931` (arm64 macOS-config requires FSIOC_KERNEL_ROOTAUTH sealed root for non-ramdisk roots). Patch **0029** (`dd1c284`): SWEET logs `SWEET: root volume is not authenticated (error N); continuing unsealed` instead of panicking (Apple configs keep the panic; pivot_root still requires auth). CI **`38797924912`** running. Next: download out-0029, phone image with sweet-0028.adt + rd=ufsroot0 (no RAM disk), boot -> expect init from UFS.
- **UFS ROOT DRIVER VERIFIED ON HARDWARE (read-only).** 0028 CI `38796139570` PASSED, `xnu/out-0028` SHA256 `8b981640...9da6`; QEMU HFS RAM-disk regression `research/q0028-hfs` still runs init. Phone boot 2026-10-15 19:50:24 UTC (`research/boot0028u.log`, image `shim/out/shim-boot-0028u.img`, phone ADT `shim/out/sweet-0028.adt` with /sweet-ufs, NO RAM disk, `rd=ufsroot0`), photos IMG_20261015_215151/215220: `SWEET UFS: "userdata" on LUN 0: first block 0x2b8c00, 109643 MiB, read-only` -> `BSD root: ufsroot0, major 1, minor 0` -> `hfs_ValidateHFSPlusVolumeHeader: unknown Volume Signature : f188` (Android-encrypted data, as expected) -> `cannot mount root, errno = 19`, kernel retries mountroot (no panic). Nothing written to flash. NEXT (destructive, needs user confirmation right before): `fastboot flash userdata research/root-hfs.img` (8 MB HFS with test init) then boot rd=ufsroot0 -> expect init from internal flash.
- **PATCH 0028 (`33aceac`): XNU read-only UFS root device**, CI **`38796139570`** running (next artifacts xnu/out-0028, research/bt29, run29.log). `bsd/dev/sweet_ufs.c`: reuses ABL's UFSHCI state (like the shim probe), takes over UTRL at a kmem_alloc_contig region (DMA layout: UTRL 0x0, UCD 0x1000, 1 MiB bounce 0x4000), verifies UTRLBA readback before any doorbell, IE=0 polled, one SCSI cmd at a time on LUN0 (TUR x3, READ(10) via bounce, PRDT 256 KiB chunks), flush_dcache64 by PA around each command (host not dma-coherent per DT), finds GPT partition by name (`ufsroot=` boot-arg, default `userdata`), exposes `/dev/ufsroot0`/`rufsroot0` (512-byte logical sectors, physical 4 KiB, DKIOC* ioctls), refuses writes (EROFS). IOKitBSDInit: `rd=ufsroot0` -> sweet_ufs_rootdev() (lazy init) else panic. ADT: `tools/mkadt.py` adds `/sweet-ufs` reg <0x1d84000 0x3000> for the PHONE only (not --qemu). Local compile OK, imports resolve. Sandbox research/bt28 (pre0028 originals). FIRST PHONE TEST PLAN (non-destructive): boot 0028 with phone ADT + `rd=ufsroot0` and NO RAM disk: expect `SWEET UFS: "userdata" on LUN 0: first block 0x2b8c00, 109643 MiB` + `BSD root: ufsroot0`, then root mount failure (userdata is still Android-encrypted) -> panic. That validates the driver before flashing an HFS image into userdata (needs explicit user confirmation at that time).
- **SUPERSEDES the 64 GiB iosroot GPT plan: user said "u can delete android" (2026-10-15).** Use the existing `userdata` partition (LUN0 sda34, 4K LBA 0x2B8C00, 0x1AC4B MiB ~107 GiB) directly as the iOS root: NO GPT changes. Keep bootloaders, recovery (OrangeFox), modem/persist/fsg/modemst untouched. Write the HFS image later with `fastboot flash userdata` (sparse, auto-split under the 768 MiB limit). Android will no longer boot normally (expected; reinstallable). Order: XNU UFS driver first, read-only against the current GPT; flash only once it can locate userdata. Nothing erased yet.
- **DECIDED (user, 2026-10-15): 64 GiB `iosroot` on LUN0 (sda), taken from userdata (wipes Android user data; Android/super kept).** Planned GPT (4K LBAs): userdata 0x2B8C00..0xD7D6FF (~43 GiB, keep type/unique GUIDs and attributes); iosroot 0xD7D700..0x1D7D6FF (0x1000000 blocks = 64 GiB, 1 MiB aligned); backup GPT area unchanged. Procedure: user backs up phone data -> boot OrangeFox recovery (adb) -> back up GPT (sgdisk --backup + raw dd of LBA 0-5 and last 6 blocks) to research/ -> sgdisk edit -> Format Data -> verify Android boots to setup. NOT EXECUTED YET.
- **INTERNAL UFS WORKS FROM THE SHIM (read-only), user authorized internal-UFS approach incl. flashing (2026-10-15, "i allow everything").** Shim probe `shim/ufs.c` (commit `12ed3ed` + filter): ufshc@1d84000 is UFSHCI 2.1 (ver 0x210, cap 0x1587031F, 32 slots), ABL leaves HCE=1, HCS=0x10F (device present, lists ready, UIC ready); probe takes over UTRL at its own buffer (verifies UTRLBA readback before any doorbell), TEST UNIT READY/READ CAPACITY/READ(10) all succeed. LUN0 = 0x1D7D800 x 4 KiB (~126.6 GB), valid GPT (64 entries at LBA 2). Photos IMG_20261015_212916/213325/213429. Big LUN0 partitions (start in 4K LBAs / MiB): super 0xCA00/0x2200 (8.5 GiB), logdump 0x22CA00/64, minidump 0x230A00/128, rawdump 0x238A00/128, recovery 0x240C00/128, cache 0x248C00/384 ext4, exaid 0x260C00/384, cust 0x278C00/1024 ext4, **userdata 0x2B8C00/0x1AC4B MiB (~107 GiB), fs unreadable = encrypted (metadata encryption)**. cache..cust contiguous = only 1.75 GiB (too small for iOS ~10+ GiB). Plan needing user consent: shrink userdata in GPT (wipes Android data) and add an `iosroot` partition at the end of LUN0. LUNs 1-5 not listed yet (sdb..sdf backed up in backup/lun).
- **fastboot limit measured (2026-10-15, read-only getvar):** `max-download-size: 805306368` (768 MiB); `max-fetch-size` unsupported; `is-userspace: no` (ABL fastboot). A `fastboot boot` image (shim+kernel+RAM disk) must be < 768 MiB, so the 2.5 GiB iOS shared cache cannot ride in the boot RAM disk even if XNU gets all RAM. Real userland needs storage access (UFS driver, or USB host) or a much smaller cache. Unlocking full RAM is still needed later (GUI).
- **iOS 16 USERLAND SOURCE (Claude, 2026-10-15).** Chose **iPhone X (iPhone10,3/10,6, D22/D221AP)** iOS **16.7.16 (20H392)** over iPhone 8: same A11 (no PAC -> plain arm64, runs on Kryo 470/ARMv8.2), last iOS for A11; X userland is home-button-less and tall-screen (1125x2436 vs sweet 1080x2400), better fit for later GUI. Note the X has its OWN SystemOS cryptex (different digest from iPhone 8's). IPSW URL in `research/ipsw-x/URL.txt` (+ BuildManifest). Images on the user's external SSD (NOT in git, do not format that disk: it holds the user's photos): `/run/media/dipa/android/ios-sweet-ipsw/iPhone10,3_16.7.16_20H392/` — `087-86593-047.dmg` = Cryptex1,SystemOS (2,743,074,816 B), `087-86698-044.dmg` = OS rootfs (4,730,814,044 B), both zip CRC-32 verified, plus Firmware/*.root_hash/.trustcache/.mtree. AppOS cryptex is 087-86715-047.dmg (10 MB, same as iPhone 8).
  - Images are raw APFS; files use LZBITMAP (decmpfs type 14). 7z cannot decode. Tools: `ipsw` 3.1.738 (AUR ipsw-bin, user-approved) needs apfs-fuse on Linux; **apfs-fuse** built in user space (system cmake broken: cmake 4.4.4 wants libjsoncpp.so.27, jsoncpp 1.9.6 installed = partial upgrade, NOT fixed) with pip cmake + `-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"`, installed to `~/.local/bin/apfs-fuse` (+apfs-dump-quick). Mount via a comma-free symlink (apfs-fuse splits options on ',' in `iPhone10,3`; exFAT has no symlinks -> put link elsewhere): `apfs-fuse LINK/087-86698-044.dmg MNT` -> `MNT/root/...`; unmount `fusermount3 -u MNT`.
  - Verified from headers: `dyld_shared_cache_arm64` magic `dyld_v1   arm64`; `/sbin/launchd`, `/usr/lib/dyld` = cputype 0x100000c subtype 0 (plain arm64). libSystem exists only inside the cache.
  - **BLOCKER for real userland via RAM disk:** shared cache = 3.10 GiB in 44 files (~2.5 GiB without the 592 MB `.symbols`), but the kernel manages only 1.20 GiB (0xb0000000..0xfd000000). Options: (A) give XNU the phone's full ~6 GB (banks 0x80000000+2G and 0x100000000+0xfcc00000, carve-out holes need pmap support) AND check fastboot max-download-size for a multi-GB boot image; (B) storage driver (Qualcomm UFS) to read a root from flash (no flashing allowed: would need a file/partition strategy); (C) run against individual dylibs extracted from the cache (uncertain).
- **MILESTONE ON HARDWARE: HFS+ ROOT FILESYSTEM -> PID 1.** Boot 2026-10-15 18:22:12 UTC (`research/boot0027b.log`, image `shim/out/shim-boot-0027b.img`), photo `IMG_20261015_202334.jpg`: `BSD root: md0` -> `hfs: mounted SweetRoot on device b(1, 0)` -> `load_init_program: attempting to load /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` -> repeated `SWEET init: still alive`, no panic. Kernel 0027 (built-in Apple OSS HFS + BearSSL MD5), init with SP alignment fix. NEXT: real iOS 16 userland on the HFS image (dyld, libSystem/dyld shared cache, launchd). Known gaps: entitlements (none granted), SHA-1/SHA-384/512 providers, no storage driver (RAM disk only), root mounted read-only.
- **PHONE HFS ROOT (0027) + SIGBUS ROOT-CAUSED.** Phone boot 2026-10-15 17:49:06 UTC (`research/boot0027.log`, photo `IMG_20261015_200031.jpg`): `hfs: mounted SweetRoot on device b(1, 0)` (HFS works on hardware), init printed its first line, then `pid 1 exited -- exit reason namespace 2 subcode 0xa` = SIGNAL/SIGBUS -> `initproc failed to start` panic. Diagnostic boot 18:08:13 UTC (`research/boot0027d.log`, image `shim/out/shim-boot-0027d.img`, diag init `research/diag-init` from scratch diag-init.S with [a][b][c] markers, `cs_debug=1`; photo `IMG_20261015_201547.jpg`): output `PID [a][b]` then SIGBUS -> fault at first `strb w21,[sp]`; no code-signing messages. Cause: kernel enters a static binary with sp at argc, not necessarily 16-aligned (depends on arg/apple string lengths, differs mockfs vs HFS); EL0 SP alignment check -> EXC_ARM_SP_ALIGN -> SIGBUS (sleh handle_sp_align). QEMU does not enforce SP alignment. NOT a kernel bug: real binaries start in dyld which aligns. Fix `1a8fc15`: sweet-init aligns sp at entry. Rebuilt `research/root-hfs.img`, QEMU `research/q0027-hfs2` OK. Phone image `shim/out/shim-boot-0027b.img` SHA256 `098ba46c...c5c` (payload-0027b, ramdisk match check-0027b) ready, awaiting FASTBOOT. Lesson: Assembly `;` is a comment in Apple arm64 asm (use .macro, not ;-joined #define).
- **HFS ROOT WORKS (QEMU).** 0027 CI `38779441895` PASSED, `xnu/out-0027` SHA256 `a8643886...5f6a`. QEMU `research/q0027-hfs` (RAM disk = `research/root-hfs.img`, rd=md0): `BSD root: md0` -> `hfs: mounted SweetRoot on device b(1, 0)` -> `load_init_program: attempting to load /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` + heartbeat, no panic. Phone image `shim/out/shim-boot-0027.img` (payload-0027 = 0027 kernel + sweet-0021.adt + root-hfs.img + rd=md0, ramdisk byte-match check-0027) ready; awaiting FASTBOOT for phone test.
- **HFS ROOT WORK (Claude), patches 0026-0027.**
  - 0026 (`f0e990c` + link fix `64c96f4`): Apple OSS **hfs-627.100.6** (commit `31d5ec238dfab485e78a9a30b8b92d13e8914f03`, paired with xnu-8796.141.3 in macos-135 release.json) staged by `xnu/patches/0026-stage-hfs.sh` into `bsd/sweet_hfs` (core + hfs_encodings; AppleKeyStore include/key prototypes guarded by CONFIG_PROTECT; SDK TargetConditionals copied). New config option `sweet_hfs` (MASTER + MASTER.arm64.sweet); 40 `optional sweet_hfs` files; Makefile.template foreach adds `_CFLAGS_ADD = -include sweet_hfs_compat.h -I...` (MUST come after kernel -D flags; compat undefs BSD/XNU_KERNEL_PRIVATE and the CONFIG_* HFS tests so kext-config.h decides: macOS profile, no content protection). `sweet_hfs_iokit.c` replaces hfs_iokit.cpp/hfs_encodings_kext.cpp: `sweet_hfs_register()` called in bsd_init after vfsinit (encodings init, vfs_fsadd VFS_TBLCANMOUNTROOT, zones, sysctls); is_ejectable 0, iterate_media none, serial KERN_NOT_SUPPORTED, OSKextGetCurrentLoadTag -> kOSKextInvalidLoadTag (renamed via compat macro). hfs_encodings' own `hfs_allocated` renamed per object (separate kexts on Apple; first CI `38777325964` failed on that duplicate at link). mockfs now validates Mach-O magic (its TODO) so non-Mach-O roots fall through to HFS (new fs entries come AFTER mockfs in vfsconf). CI `38778133720` PASSED, `xnu/out-0026` SHA256 `0b49170c...701a`, prints `SWEET HFS: hfs-627.100.6 registered`.
  - HFS+ image: `research/root-hfs.img` (8 MB, mkfs.hfsplus from AUR hfsprogs installed with user approval; loop-mounted with sudo; /sbin/launchd = research/sweet-init, /dev; fsck.hfsplus OK). QEMU `research/q0026-hfs`: `BSD root: md0` then data abort in MD5Init from hfs_getvoluuid (mount computes volume UUID).
  - 0027 (`14bf2b7`): 0020 stager also copies BearSSL hash/md5.c + codec/dec32le.c/enc32le.c; `corecrypto_md5.c` SWEET branch converts MD5_CTX <-> br_md5_context around br_md5_* calls; wrappers sweet_md5.c/sweet_dec32le.c/sweet_enc32le.c. Host test 40/40 match md5sum. CI **`38779441895`** running.
  - Local tooling: `tools/xnu-cc.py` has `XNU_CC_POST` (flags after logged ones). Sandbox `research/bt28/xnu` = 0025 CI tree + 0026/0027 edits; originals `research/pre0026`, `research/pre0027`. HFS local objects `research/hfs-objs`. Checker entries updated to bt28.
  - SHA-1 / SHA-384/512 still have no provider.
- **MILESTONE ON HARDWARE: USERSPACE PID 1 RUNS ON THE PHONE.** Boot 2026-10-15 17:16:00 UTC (`research/boot0025.log`: send OKAY0.410s, boot OKAY0.135s), image `shim/out/shim-boot-0025.img` SHA256 `9dbfa0b1...89db0`. Photo `IMG_20261015_191733.jpg`: `BSD root: md0` -> `load_init_program: attempting to load /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` -> repeated `SWEET init: still alive` (5 s heartbeat), no panic. Kernel 0025 + mockfs RAM disk + built-in no-entitlements AMFI. No flashing performed.
- **NEXT (toward real iOS userland):** real root filesystem with real binaries. Options: (a) build Apple OSS `hfs` (macOS 13.5 tag `hfs-627.100.6`) into SWEET like libpthread, then an HFS RAM disk image; (b) iOS 16 dyld + libSystem/dyld shared cache from the IPSW (needs APFS or repacking onto HFS). Expected hurdles: entitlements (sweet_amfi grants none), SHA-1/MD5/SHA-384/512 providers (BearSSL), missing kexts/daemons. Current init is a static test program, not launchd.
- **MILESTONE: USERSPACE RUNS (QEMU).** 0025 CI `38774788048` PASSED, `xnu/out-0025` SHA256 `5496069b...4877`. QEMU `research/q0025-rd` (150 s, no panic): `BSD root: md0` -> `load_init_program: attempting to load /sbin/launchd` -> `*** SWEET userspace: init running as PID 1 ***` -> `SWEET init: still alive` every 5 s. Exec, ad-hoc SHA-256 code signature, syscalls, devfs /dev/console, select timeouts all work. Phone image prepared: `shim/out/shim-boot-0025.img` 16,515,072 bytes SHA256 `9dbfa0b15140c0bd733a16c4f727f6a7df44a8741f98d8e266d5f2fa4f689db0` (payload-0025 = 0025 kernel + sweet-0021.adt + research/sweet-init + rd=md0; ramdisk byte-match check-0025). Waiting for user to put phone in FASTBOOT.
- **0024 CI `38773277811` PASSED**, `xnu/out-0024` SHA256 `a0ba964b...4348`. QEMU `research/q0024-rd` and **PHONE (boot 2026-10-15 17:05:55 UTC, `research/boot0024.log`, image `shim/out/shim-boot-0024.img` SHA256 `cf3dac86...6964`, 16,510,976 bytes, payload `shim/out/payload-0024.bin` = 0024 kernel + `shim/out/sweet-0021.adt` + RAM disk `research/sweet-init` + rd=md0; ramdisk byte-match `check-0024`)** agree: `Added memory device md0`, `BSD root: md0`, `load_init_program: attempting to load /sbin/launchd`, then data abort in accelerate_entitlement_queries (PC 0x...77d2458, FAR 0x1a0) = NULL AMFI interface. Photo `IMG_20261015_190715.jpg`. RAM disk root is physically verified.
- **0025 (`2518c79`)**: new `bsd/kern/sweet_amfi.c` registers a built-in AMFI interface at EARLY_BOOT/MIDDLE: no entitlements (adjustContextWithoutMonitor succeeds without creating a context, queries KERN_DENIED, copies NULL/KERN_NOT_FOUND, get_xml/transmuted false); WithMonitor KERN_NOT_SUPPORTED; OSEntitlements_query and query_context_to_object panic explicitly (CoreEntitlements closed; no kernel caller); TrustCache version 0 (0015 keys on trust_cache_rt==NULL, img4 absent -> still unavailable); CoreEntitlements table zero (PMAP_CS only). Compiles locally; checker updated (bt27 entries). CI **`38774788048`** running. Next artifacts `xnu/out-0025`, `research/bt29`, `research/run29.log`. Real iOS binaries will need real entitlements later (no OSS CoreEntitlements exists).
- **0021-0024 RAM-disk-root iteration (Claude), QEMU only so far, no new phone boot since 0020.** Each kernel tested with `tools/mkpayload.py KERNEL research/q0021.adt OUT --ramdisk research/sweet-init --cmdline "debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress rd=md0"` then `tools/qemu-sweet.py OUT DIR --secs 150 --kernel KERNEL --fail-on-panic` (PATH needs ~/swift/usr/bin for llvm-nm). Fresh ADTs: `research/q0021.adt` (QEMU), `shim/out/sweet-0021.adt` (phone).
  - 0021 CI `38769532000` PASSED, `xnu/out-0021` SHA256 `2b7500a1...962e`, `research/bt25`/`run25.log`. QEMU `research/q0021-rd`: md0 root, then assert `iomediaClass` in IOService::publishHiddenMedia.
  - 0022 (`20ece9c`, publishHiddenMedia without IOMedia class) CI `38770666158` PASSED, `xnu/out-0022` SHA256 `a4d0e787...30c5`, `research/bt26`/`run26.log`. QEMU `research/q0022-rd`: data abort in SHA256_Init (NULL g_crypto_funcs) from vm_kernel_addrhash_internal.
  - 0023 (`c8089d1`, libkern SHA256_Init/Update/Final use built-in ccsha256_ltc_di + ccdigest_init/update on SWEET; also serves code-signing SHA-256; SHA-1/MD5/SHA-384/512 still have NO provider - BearSSL from 0020 could supply them later) CI `38771876423` PASSED, `xnu/out-0023` SHA256 `d763fed7...1003`, `research/bt27`/`run27.log`. QEMU `research/q0023-rd`: **mockfs mounted md0, `load_init_program: attempting to load /sbin/launchd`**, then panic `mockfs_fsnode_vnode failed to create fictitious pages ... rvalue = 5` (mockfs.c:351).
  - 0024 (`3293c80`): mockfs used memdev mi_base (4K page number of a VIRTUAL kernel address for IOKit RAMDisk, truncated to 32 bits) << PAGE_SHIFT as a physical address. Now zero-copy pager mapping only when mi_phys (with 4K units); virtual md uses pagein/blockmap/strategy -> memdev bcopy. memdev's own mdPhys path assumes 4K ppnums (unsafe on 16K arm64), so do NOT switch the RAMDisk to physical. CI **`38773277811`** running. Next artifacts `xnu/out-0024`, `research/bt28`, `research/run28.log`.
  - Sandboxes: bt25/bt26/bt27 contain the next patch's edited files; pristine copies in `research/pre0022`..`pre0024`.
- **PATCH 0021 (Claude): RAM disk root via mockfs -> first userspace program.** Commits `9c37771` (initial; CI `38768500280` FAILED: mockfs_fsnode.c kfree_type(mockfs_fsnode_t) typed-alloc static assert; local xnu-cc stubs `__builtin_xnu_types_compatible` so it can't catch this) and `193772f` (fix), CI **`38769532000`** running: https://github.com/dipa-studio/ios-sweet/actions/runs/38769532000. Next artifacts: `xnu/out-0021`, `research/bt25`, `research/run25.log`.
  - Kernel: `config/MASTER.arm64.sweet` FILESYS_BASE += `mockfs` (XNU's built-in FS: root device is itself a Mach-O, served as /sbin/launchd, plus /dev for devfs). Upstream mockfs bit-rot fixed: kalloc_type/kfree_type used the POINTER typedef mockfs_fsnode_t (nodes were 8 bytes) -> `struct mockfs_fsnode`. `IOService::setRootMedia` asserted IOBlockStorageDriver class exists (IOStorageFamily absent): SWEET-only, a missing class means root=NULL (same as IOSetImageBoot); Apple path keeps assert. All 21 patches apply (`research/check-patch-order.py`); 5 TUs compile locally with -DMOCKFS (`research/mockfs-objs`), imports resolve vs 0020 kernel. Sandbox `research/bt24/xnu` now holds 0021 versions of those 3 files; pristine 0020 copies in `research/pre0021`.
  - Tools: `tools/mkpayload.py --ramdisk IMAGE` appends IMAGE as an extra payload segment right after the kernel, moves kernel_end past it (shim unchanged: ADT+boot_args follow it, image stays below topOfKernelData), and adds ADT `/chosen/memory-map` `RAMDisk`=<phys,size>. Boot with `rd=md0`. Without --ramdisk output is byte-identical to existing 0020 QEMU and phone payloads (verified). `tools/sweet-init.S` + `tools/mkinit.py`: PIC static init (open /dev/console, print PID, heartbeat every 5 s via select, never exits) wrapped into a static arm64 Mach-O (__PAGEZERO, __TEXT@0x100000000, LC_UNIXTHREAD, ad-hoc SHA-256 CodeDirectory v0x20400, 4K pages). Build: `~/swift/usr/bin/clang -target arm64-apple-ios16.0 -c tools/sweet-init.S -o X.o && tools/mkinit.py X.o research/sweet-init` (ld64.lld cannot link static iOS). Signature not verified by a real codesign; the kernel will tell.
  - QEMU with 0020 kernel + RAM disk (`research/q0020-rd.bin`, rd=md0, 150 s `research/q0020-rd150`): `Added memory device md0`, **`BSD root: md0, major 1, minor 0`** (plumbing works), then the setRootMedia assert (now fixed in 0021). NOTE: QEMU needs >60 s to reach IOFindBSDRoot (60 s run showed nothing after stf0; likely RTC/inittodr wait), so use --secs 150.
  - NEXT: download 0021, QEMU 150 s with ramdisk; expect mockfs mount then init exec -> `*** SWEET userspace: init running as PID 1 ***`. Then phone boot with same payload (phone ADT shim/out/sweet-0020.adt pattern + --ramdisk + rd=md0).
- **0020 PHONE RESULT (user transcription of screen, 2026-10-15): KERNEL INIT COMPLETE, WAITING FOR ROOT.** After Skywalk self-tests PASSED, kminit, ifnet_attach lo0/gif0/stf0, screen prints `BSD`, then `Waiting on <dict><key>IOProviderClass</key><string>IOMedia</string><key>Content</key><string>Apple_HFS</string></dict>` and `Failed to write boot-picker-bringup-reason to NVRAM.` No panic. This is IOFindBSDRoot waiting for a root device (none exists); NVRAM warning is expected (no NVRAM driver). QEMU `cpu_idle` snapshot is presumably the same wait (not GDB-confirmed; milestone GDB run was cancelled by user since phone answered it). Draft `research/check-milestones20.gdb` (hbreaks inittodr/vfs_mountroot/IOFindBSDRoot/bsd_utaskbootstrap) left uncommitted/ignored.
- **NEXT MILESTONE: root filesystem -> launchd.** Plan: (1) RAM disk: shim places an image in RAM, ADT /chosen/memory-map `RAMDisk` entry, boot-arg `rd=md0` (XNU md driver). (2) HFS is a separate kext on Apple; build Apple OSS `hfs` (tag paired with macOS 13.5 in distribution-macOS macos-135 release.json) into SWEET like libpthread 0019. APFS is closed. (3) Minimal HFS image with iOS 16 launchd (later: dyld/shared cache; AMFI/code-signing absent will be a later hurdle). User's end goal is full iOS GUI.
- **0020 (AES) VERIFIED + PHONE BOOTED (Claude took over after Codex usage limit).** Actions `38765422261` (`ab9e23f`) PASSED; kernel `xnu/out-0020/kernel.development.sweet` checksum OK; buildtree `research/bt24`, log `research/run24.log`. Actual-kernel AES checks PASS (`research/check-aes20.gdb` / `.log`, 12 extra CBC cases match OpenSSL). 45s QEMU `research/q0020`: no panic, zero unknown glyphs, past old TCP/AES panic -> Skywalk self-tests PASSED -> bsd_autoconf kminit -> ifnet_attach lo0/gif0/stf0; final snapshot PC in `cpu_idle` (idle or waiting; NOT yet diagnosed, could be root-device wait). Image `shim/out/shim-boot-0020.img` 16,461,824 bytes SHA256 `775200cb02d95fc750981516b79d91dcdc96184bbe67a38f9d57c59b4a391828`, ramdisk byte-matches `payload-0020.bin` (`check-0020`). **Phone temporarily booted 2026-10-15 16:10:00 UTC**: one FASTBOOT product sweet, send OKAY0.407s, boot OKAY0.134s, total0.549s, exit0 (`research/boot0020.log`). Awaiting user photo; do not reboot the phone before it. NEXT: read photo; diagnose what QEMU waits on after stf0 (GDB backtrace of threads / longer run). Codex's STOP request below is complete.
- User requests STOP after completing the current0020 AES iteration (Actions/QEMU/phone), then leave a precise committed handoff for another AI. User says FASTBOOT ready again after0019 photo. Do not start a new blocker fix after this iteration.
- Goal: iOS16-generation XNU on Redmi Note10Pro sweet/SM7150. Always commit, use gh/Actions for kernel builds, keep this handoff current. Temporary fastboot boot only; preserve Android/backups. No userland/root filesystem boot yet.
- **Patch0020 update:** initial `e71098d` / Actions `38765012466` superseded by scalar-only compile flags. Added -mgeneral-regs-only to all six provider/BearSSL TUs and corecrypto_aes.c; object disassembly now confirms no SIMD. Updated implementation `ab9e23f`, Actions `38765422261` queued/running: https://github.com/dipa-studio/ios-sweet/actions/runs/38765422261. Do not launch another run. portable BearSSL AES CBC and legacy one-block ECB wired directly into SWEET corecrypto_aes.c. Genuine bitslice implementation, no CPU AES/SIMD requirement, heap or fake global crypto table. Source fetched by `0020-stage-bearssl.sh` from official BearSSL git at immutable `7bea48e5e850ab4cafbe68d3765cdaba13a86d6f`; MIT license retained, downloaded source stays out of git. Kernel limits include adapter only. All128/192/256-bit keys, CBC encrypt/decrypt, copied contexts, in-place/out-of-place; zero block count and size overflow handled, invalid keys clear context. Seven TUs compile to arm64 objects (`research/aes-objs`), imports resolve, all20 patches apply and sources match (`research/patch0020-check.log`). Sandbox `research/bt23/xnu`, originals `research/pre0020`. NEXT: Actions then actual-kernel NIST/random CBC/ECB checks and >=45s QEMU before another phone boot. Next artifacts out-0020 / bt24 / run24.log. 0020 updated CI pending. Local sources/checks match the scalar-only patch.
- **Latest built kernel0019**, implementation `256d3f0`, patches0001-0019. Actions `38763072907` PASSED (4m5s): https://github.com/dipa-studio/ios-sweet/actions/runs/38763072907. Kernel `xnu/out-0019/kernel.development.sweet`, SHA256 `a27df7568f03fb541cf0e8024c0a594fa178a85054fcf9eba29158cbd4c5ecc0`; checksum verified. Log/source `research/run23.log` / `research/bt23` (no nested .git). Seven pthread source/build/header files and staged adaptations byte-match local compile inputs.
- **0019 hardware photo received** `IMG_20261015_175306.jpg`: pthread registration visible, passes mbinit and DLIL checksum/scheduling, same AES fault as QEMU: PC`0xfffffe00079174e0` (`aes_encrypt_key128`), LR`0xfffffe000767c3a4` (`tcp_init`), FAR0x78, x8=0. Disassembly proves NULL g_crypto_funcs->ccaes_cbc_encrypt. Photo processed; no more0019 observation pending. General crypto table remains absent, other hash/mode operations may still fault after0020. DO NOT bypass TCP or install fake crypto. XNU already contains SHA256/HMAC implementations; AES source there is a dispatch wrapper. Full future provider interface libkern/libkern/crypto/register_crypto.h. Apple/corecrypto default README describes evaluation-only licensing, so no external corecrypto was fetched or staged; BearSSL selected for this portable AES integration.
- **0019 pthread FIXED in QEMU and observed beyond its init on phone**: 45s QEMU `research/q0019`, zero unknown glyphs, then AES panic. Actual-kernel API checks PASS `research/check-pthread19.gdb` / `.log`, guest `research/q0019-api`: function/callback table identity, actual provider pthread_init returns, locks/global hash/threadcall/typed zones initialized, thread_selfid via syscall shim + buffer bounds, process hash init/delete via shims restores NULL. Monitor quit prints expected remote-disconnect after PASS. All fixture guests stopped.
- **0019 provider**: genuine Apple libpthread-514.100.2 commit `a6c70ca0e544347a0377babf748f831577ec24f9`, exact paired release with xnu8796.141.3 in distribution-macOS/macos-135/release.json (`research/macos-135-release.json`). 0019 shell stage runs before patches (workflow copies *.sh); three SWEET wrapper TUs use kext BSD header view, vm_fault_external, renamed provider-side pthread_kern/current_uthread; register real function table at EARLY_BOOT SECOND, real pthread_init later. Stager applies bounded address casts, zero user addresses, proper panic format. SWEET opaque ksyn storage enlarged48->56 for trailing kwe_uth, checked with provider static assertion. Local pre-CI TargetConditionals fixture was syntax-only; artifact bt23 carries actual SDK header and CI is authoritative.
- **Phone0019 temporarily booted** 2026-10-15 15:51:36 UTC: exactly one FASTBOOT/product sweet verified; send OKAY0.406s, boot OKAY0.134s, total0.546s, exit0 (`research/boot0019.log`). Image `shim/out/shim-boot-0019.img`, 16,457,728 bytes, SHA256 `19429a57e486b856880df9e27327ed488e0139345311fdb0350974e72103a9a1`. Phone ADT/payload `shim/out/sweet-0019.adt` / `payload-0019.bin` timer19, unpacked ramdisk byte-match (`check-0019`). EntryPA0xb146b488, kernelendPA0xb2000000, boot_argsPA0xb2004000/top0xb2008000. Last user FASTBOOT+USB reply preceded this boot; do not assume phone is still in FASTBOOT.
- **CPU0018 physically verified** by `IMG_20261015_173951 (2).jpg`: boot CPU0 registered, real SGI1 verified, initialization complete -> BSD/MAC/mbinit -> old pthread-table panic. SweetCPU uses legacy IOCPU, ml_processor_register/processor_start/initCPU, real SGI1 clears SIGPdisabled before max CPUs completion; no forced flags/skipped waits. CPU APIs and two-CPU rejection passed (`research/check-cpu18.gdb`, `q0018-api`, `q0018-smp`). Single core; SMP/suspend unsupported. CURRENT SWEET has NO USE_APPLEARMSMP.
- QEMU next run: generate fresh `tools/mkadt.py research/q0020.adt --qemu` (timer27), mkpayload with `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress`, then `tools/qemu-sweet.py PAYLOAD OUT --secs 45 --kernel KERNEL --fail-on-panic`. 12s can miss later panics because IORTC waits30s. Runner snapshot only: panic exit2, blank/incomplete exit3; absence of panic does not mean complete boot. Qualcomm XPU/ABL/panel and phone SMC PSCI require hardware checks.
- Local checker `tools/xnu-cc.py research/run23.log research/bt23 FILTER --per-file`, XNU_CC_OBJDIR for objects. For new BearSSL TUs add XNU_CC_EXTRA with -I bt23/xnu/libkern/crypto/sweet_bearssl/{inc,src}. Patch checker `research/check-patch-order.py` uses temporary upstream index; source fixtures/artifacts ignored under research. GDB mask registers to unsigned64, use function-entry manual-call return traps, discard manipulated guests. No QEMU guest intentionally left running.

## Session log and earlier handoffs (superseded by the active summary above)

### Prior 0019 transition notes (superseded)

- Goal: iOS 16 generation XNU on sweet. Always commit completed work, use gh/Actions for kernel builds, keep this handoff current. Temporary fastboot boot only; preserve Android/backups.
- **0019 build PASSED** Actions `38763072907` (4m5s), implementation `256d3f0`. Kernel `xnu/out-0019/kernel.development.sweet`, SHA256 `a27df7568f03fb541cf0e8024c0a594fa178a85054fcf9eba29158cbd4c5ecc0`; checksum verified. Log/source `research/run23.log` / `research/bt23`; all seven patched files and staged provider adaptations match local compile inputs. Prefer bt23 as the next baseline.
- **0019 pthread FIXED in QEMU**: 45s run `research/q0019` registers provider, passes mbinit, DLIL SUM16 checks and DLIL thread scheduling. Actual-kernel GDB checks `research/check-pthread19.gdb` / `.log` PASS: function/callback table identity, real _pthread_init returns, locks/global hash/cleanup threadcall/typed zones non-NULL, thread_selfid via actual syscall shim with output bounds, process hash init/delete through actual shims restores NULL. Guest stopped after tests; no fixture left running. GDB monitor quit prints expected remote disconnect after PASS.
- **Current next blocker:** QEMU0019 panic data abort FAR0x78 at `aes_encrypt_key128` PC`0xfffffe00079174e0`, called by `tcp_init` LR`0xfffffe000767c3a4`. Disassembly confirms `g_crypto_funcs == NULL`, loading ccaes_cbc_encrypt at+0x78. General crypto registration is still absent; DO NOT bypass TCP or supply fake crypto. Next integrate a genuine compatible crypto implementation/provider (AES first needed here, other digest/mode APIs also used). XNU contains SHA256/HMAC but no AES implementation; its corecrypto_aes.c is a dispatch wrapper. Full crypto table is in libkern/libkern/crypto/register_crypto.h.
- **0019 phone image prepared and verified**, user reports FASTBOOT+USB again; exactly one device/product sweet verified. `shim/out/shim-boot-0019.img`, 16,457,728 bytes, SHA256 `19429a57e486b856880df9e27327ed488e0139345311fdb0350974e72103a9a1`; unpacked ramdisk matches phone payload (`shim/out/check-0019`, `payload-0019.bin`, `sweet-0019.adt`, timer19). EntryPA0xb146b488; kernelendPA0xb2000000; boot_argsPA0xb2004000/top0xb2008000. Usual debug cmdline cpus=1/-noprogress. **Temporarily booted 2026-10-15 15:51:36 UTC**: send OKAY0.406s, boot OKAY0.134s, total0.546s, exit0 (`research/boot0019.log`). User asked for photo after45s. Await the photo; DO NOT reboot again before observation. Bootloader acceptance only; no0019 physical progress observed yet.
- **Hardware photo confirms 0018 progress** (`IMG_20261015_173951 (2).jpg`): real phone prints `SWEET CPU: boot CPU 0 registered, SGI1 verified, initialization complete`, enters BSD/MAC, reaches `mbinit: done`, then the same missing-pthread-function-table panic as QEMU at pthread_shims.c:318. CPU completion/interrupt delivery is now physically observed. The photo was received; no further phone boot performed yet.
- **Patch0019 committed/pushed `256d3f0`; Actions `38763072907` queued/running:** https://github.com/dipa-studio/ios-sweet/actions/runs/38763072907. Do not launch a duplicate. genuine Apple OSS libpthread-514.100.2 provider (immutable commit `a6c70ca0e544347a0377babf748f831577ec24f9`), paired with xnu-8796.141.3 by Apple's `distribution-macOS/macos-135/release.json` (`research/macos-135-release.json`). `0019-stage-libpthread.sh` fetches/stages it before patches; workflow now copies shell scripts too. SWEET-only wrappers compile all three real kernel sources, rename provider-side symbols, use kext header view and vm_fault_external, register the real function table at EARLY_BOOT SECOND. Existing pthread_init then runs the actual provider initialization. No pthread wait/syscall is bypassed. Source staging includes bounded explicit address casts/zero user addresses and panic-format fixes. SWEET kernel opaque ksyn storage enlarged from48 to56 bytes to contain this provider's trailing kwe_uth, with a provider size assertion. Local compile of three provider TUs plus pthread_shims/kern_fork passed; imports resolve and provider exports do not collide with existing kernel symbols. All19 patches apply and changed sources match (`research/patch0019-check.log`). Source sandbox `research/bt22/xnu`, originals `research/pre0019`, objects `research/pthread-objs`. Local TargetConditionals fixture only has target macros for syntax; CI stages the real macOS SDK header and is authoritative. Next Actions artifacts: out-0019 / bt23 / run23.log; run QEMU >=45 seconds. 0019 CI is pending; no 0019 QEMU run yet.
- **Latest kernel implementation `e7d9550`, patches0001-0018; CI `38760004398` PASSED (3m56s)**: https://github.com/dipa-studio/ios-sweet/actions/runs/38760004398. No build is pending. Kernel `xnu/out-0018/kernel.development.sweet`, SHA256 `b3c8d59acc88f93a2c836eb2527a05ed4fbaac7c580a2ea8c3b795137f4aadf1`; checksum verified. Source/log `research/bt22` / `research/run22.log` (baseline for patch0019; no nested .git). New driver/config sources match CI exactly.
- **CPU wait FIXED in QEMU**: SweetCPU uses legacy IOCPU, validates single boot CPU, ml_processor_register -> processor_start -> initCPU real SGI1 -> scheduler/IOKit completion -> ml_set_max_cpus. It does NOT force a flag or skip a wait. CPU APIs passed: Running CPU0, processor_array[0] association, SGI signalling enabled, name, unsupported secondary-start error (`research/check-cpu18.gdb` / `.log`, `research/q0018-api`). Single-core only; SMP and suspend unsupported. Two-CPU topology fails explicitly (`research/q0018-smp`).
- **Next blocker reproduced in 45s QEMU** (`research/q0018-long`, zero unknown glyphs): BSD initialization reaches MAC framework, network mcache and `mbinit: done`, then **`pthread kernel extension not loaded (function table is NULL). @pthread_shims.c:318`**. Do not bypass pthread_init: find/integrate the matching open-source libpthread kernel implementation and register its real function table. A 12s run `research/q0018` looked panic-free before the 30s IORTC wait elapsed; use >=45s or GDB checkpoints for the next iteration. No userland/root filesystem boot yet.
- **Phone 0018 TEMPORARILY BOOTED** 2026-10-15 **15:31:15 UTC**: exactly one FASTBOOT device verified product sweet; send OKAY0.400s, boot OKAY0.134s, total0.539s, exit0. Local log `research/boot0018.log`. This confirms bootloader acceptance only. User was asked for a photo; await it, DO NOT reboot the phone again before their observation. No flashing performed.
- Current hardware image `shim/out/shim-boot-0018.img`, **16,437,248 bytes**, SHA256 **`b7a23595d143a76c72c9fe2cfcc5d77a8f16987b5af1a2703bbffdfa6922908c`**. Phone ADT/payload `shim/out/sweet-0018.adt` / `shim/out/payload-0018.bin` (timer19), unpacked ramdisk exactly matches payload (`shim/out/check-0018`). EntryPA0xb146b488; kernelendPA0xb1ffc000; boot_argsPA0xb2000000/top0xb2004000. Command line `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress`.
- QEMU payload/ADT `research/q0018.bin` / `research/q0018.adt` (timer27). Run `python3 tools/qemu-sweet.py research/q0018.bin research/NEW-OUT --secs 45 --kernel xnu/out-0018/kernel.development.sweet --fail-on-panic`; fresh ADT seed for a new payload. Output fb.png/fb.txt/regs.txt/qemu.log/result.json. Visible panic exit2; blank/incomplete decode exit3; no_panic_observed is snapshot-only, not successful boot. -noprogress prevents the graphical indicator corrupting console text. Qualcomm XPU/bootloader/panel and SMC PSCI still need physical validation.
- Patches0014-0017 fixed PRNG registration/direct SHA256, explicit unavailable trust-cache service with rejected loads/no matches, built-in SweetPlatformExpert, and timer PPI from ADT (phone19/default19, QEMU27). All their checks/artifacts are preserved in the history below. CURRENT SWEET has NO USE_APPLEARMSMP; do not assume gCPUIC/gAIC/gPMGR AppleARMSMP globals exist.
- Local single-file checker: tools/xnu-cc.py research/run22.log research/bt22 FILTER --per-file, output via XNU_CC_OBJDIR. Patch checker `research/check-patch-order.py` applies all18 via temporary index, matches sandbox sources. Sandbox for0018 `research/bt21/xnu`, originals `research/pre0018`, objects `research/cpu-objs`. First compiler issues were missing machine/atomic.h and nonexistent getNameSymbol; fixed and both TUs compiled successfully before CI. Fixtures/artifacts under research are local-only/ignored.
- GDB: mask int(register)&((1<<64)-1) for unsigned kernel pointers; use function-entry manual-call return traps, discard manipulated guests. No QEMU fixture is intentionally left running.

### Earlier active handoff snapshot (superseded)


- **0018 milestone:** Actions `38760004398` PASSED (3m56s), checksums passed, new CPU/build files byte-match CI tree. Kernel `xnu/out-0018/kernel.development.sweet`, SHA256 `b3c8d59acc88f93a2c836eb2527a05ed4fbaac7c580a2ea8c3b795137f4aadf1`; log/tree `research/run22.log` / `research/bt22`. QEMU `research/q0018` 12s: boot CPU registered, SGI1 verified, CPU initialization complete, **BSD initialization entered** (copyright, MAC framework, buffer headers), no panic/zero unknown glyphs. Direct CPU API checks PASS (`research/check-cpu18.gdb`, `.log`, `research/q0018-api`): CPU0 Running, processor matches processor_array[0], real SGI enabled, CPU name correct, secondary-start returns KERN_NOT_SUPPORTED. Two-CPU ADT correctly panics before registration (`research/q0018-smp`). Longer 45s run `research/q0018-long` still running; next BSD wait/fault not yet identified.
- **Prepared 0018 phone image:** `shim/out/shim-boot-0018.img`, 16,437,248 bytes, SHA256 `b7a23595d143a76c72c9fe2cfcc5d77a8f16987b5af1a2703bbffdfa6922908c`. Phone ADT/payload `shim/out/sweet-0018.adt` / `shim/out/payload-0018.bin`, usual debug args with cpus=1 and -noprogress. Unpacked ramdisk byte-compared (`shim/out/check-0018`). Entry PA remains 0xb146b488; kernel end PA 0xb1ffc000, boot_args 0xb2000000/top0xb2004000. User reports FASTBOOT+USB ready; read-only check found exactly one product sweet. NEXT: temporary fastboot boot of this verified image, record send/boot results, then await hardware photo without rebooting again.
- Patch 0018 committed/pushed **`e7d9550`**; Actions **`38760004398`** queued/running: https://github.com/dipa-studio/ios-sweet/actions/runs/38760004398. Do not launch a duplicate run.
- Active continuation: patch **0018** implemented in `iokit/Kernel/arm/SweetCPU.cpp` + arm64 source list + exact qcom,kryo470 personality. Keeps legacy IOCPU, accepts ONLY the single boot CPU, fills ml_processor_info from parsed topology with CPU object as cpu_id, calls ml_processor_register and processor_start. initCPU sends SGI1 via ICC_SGI1R_EL1 and waits (1-second deadline) for real cpu_signal_handler to clear SIGPdisabled. Only after successful startup calls ml_cpu_init_completed/cpusRunning and publishes completion/max CPUs. Secondary startup/suspend are explicitly unsupported. Local C++ compile passed for both TUs (`research/cpu-objs`), all imports resolve, object contains dsb ishst / ICC_SGI1R / isb; all18 patches apply in order and match sources. Sandbox `research/bt21/xnu`, originals `research/pre0018`; next CI artifacts should use `xnu/out-0018`, `research/bt22`, `research/run22.log`. User has the phone available; first run QEMU, then prepare/temporarily boot useful hardware progress.
- Goal: iOS 16 generation XNU on Redmi Note 10 Pro / sweet. User wants persistent progress, commits, kernel builds via `gh` Actions, and this handoff kept current across agent switches. Temporary `fastboot boot` only; preserve Android and backups.
- Latest kernel implementation: **`aa3f991`**, patches **0001-0017**. All four new Actions builds PASSED: 0014 `38748543058` (4m0s), 0015 `38749549779` (4m3s), 0016 `38750733996` (4m3s), 0017 **`38751507437` (4m7s)**. Latest: https://github.com/dipa-studio/ios-sweet/actions/runs/38751507437. No build is pending.
- **Current blocker: CPU initialization has no SWEET CPU driver.** QEMU registers SweetPlatformExpert, then kernel_bootstrap_thread waits in **kperf_init_early -> ml_wait_max_cpus**. Confirmed via actual-kernel GDB call stack (`research/locate-cpu-wait2.log` / `.symbols`) and normal-run `max_cpus_initialized == 2` (MAX_CPUS_WAIT). Do not force the flag or remove the wait. Next implement real CPU registration and completion using XNU's CPU/processor interfaces. CURRENT SWEET uses legacy IOCPU (`USE_APPLEARMSMP` is absent); enabling AppleARMSMP is a separate architectural change requiring GIC/PMGR services. No userland/root filesystem boot yet.
- Fixed this session: PC=0 unregistered PRNG callback (0014, built-in HMAC-SHA256 provider + direct entropy/write_random hashing); absent Image4/AMFI trust-cache service (0015, unavailable runtime, loads rejected and queries no-match); missing platform driver (0016, SweetPlatformExpert exact personality); incorrect QEMU timer IRQ (0017, validated ADT PPI property, phone default19/QEMU27).
- **Latest kernel** `xnu/out-0017/kernel.development.sweet`, SHA256 `055a80190ab998b4df9203f1102072dcc78faad1779a537a514e522b682b582f`. Buildtree/log **`research/bt21` / `research/run21.log`**; all nine changed C/C++/header/config files byte-match local checked sources. Checksums passed. Entry PA `0xb146b488`, kernel end PA `0xb1ff4000`. Preferred baseline for patch 0018: `research/bt21/xnu` (no nested .git).
- **QEMU verified**: `research/q0017-noprogress` ran **30 seconds**, no visible panic, **zero unknown glyphs**, IRQ exceptions returned normally (4 IRQs, tickless idle). It still waits for CPU completion, so this is NOT a completed boot. Earlier 15s run `research/q0017` captured 122 IRQs, timer value27, GICR_ISENABLER0 `0x08000002`, and CPU WAIT; graphical progress overlay corrupted 13 glyphs. Add **`-noprogress`** to keep the console readable. New runner classifies incomplete/blank decoding explicitly (exit3 with --fail-on-panic), visible panic exits2. Snapshot-only; use regs/GDB to diagnose stalls.
- Current QEMU payload/ADT: **`research/q0017-noprogress.bin` / `research/q0017.adt`**. Run: `python3 tools/qemu-sweet.py research/q0017-noprogress.bin research/NEW-OUT --secs 30 --kernel xnu/out-0017/kernel.development.sweet --fail-on-panic`. Generate fresh seed with mkadt for a new payload. QEMU virt models generic ARM execution, not Qualcomm XPU/bootloader/panel or the phone's SMC PSCI conduit.
- **Prepared phone image**: **`shim/out/shim-boot-0017.img`**, 16,416,768 bytes, SHA256 `51ff7fe30b4a6668c3452ffe1da922c5ac30cb8de60625c2a652587ecbd0ec92`. Phone ADT/payload `shim/out/sweet-0017.adt` / `shim/out/payload-0017.bin` (timer19). Unpacked ramdisk exactly matches payload (`shim/out/check-0017`). Command line **`debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam -noprogress`**. NOT sent to phone this session: known CPU-driver wait should be addressed in QEMU first. Last actual phone boot remains 0013; user's latest photo `IMG_20261015_160033.jpg` confirmed the old PRNG PC=0 panic.
- Direct QEMU/GDB API checks: PRNG 600-byte chunking, buffer bounds, changing output, zero-length reads/writes, user reseed, contended refresh, generator1 init/output PASS again on latest0017 (`research/check-prng17.gdb` / `.log`, guest `research/q0017-api`). Trust-cache unavailable-service/argument/output cases PASS on0015 (`research/check-trustcache.gdb` / `.log`). Platform registration + target/product normal/truncated/zero/null/bounds PASS on0016 (`research/check-platform.gdb`, `research/check-platform3.log`). Different platform ID correctly retains IOPanicPlatform fallback (`research/q0016-negative`). Timer PPI32 and size2 correctly panic (`research/q0017-badppi`, `research/q0017-badsize`); omitted property selects19 (`research/q0017-default`). These are API/bring-up checks, not crypto certification or SMP scheduling validation.
- Local checker **`research/check-patch-order.py`** verifies all17 patches and changed source equality using a temporary upstream git index. Fixtures/logs/artifacts under research are local-only and ignored. For GDB unsigned kernel pointers, mask `int(register_value) & ((1<<64)-1)`; x30/x29 may be rendered signed. Stop at a function entry for manual-call return traps, and discard each manipulated guest after API tests. All fixture QEMU guests have been stopped.


- User wants persistent progress toward iOS 16, commits for completed changes, builds through `gh` / GitHub Actions.
- Keep this file current at every milestone so another agent can resume immediately, including when usage limits interrupt a session.
- Patch 0017 committed/pushed `aa3f991`, Actions `38751507437` running: https://github.com/dipa-studio/ios-sweet/actions/runs/38751507437. Next artifacts: xnu/out-0017, research/bt21, research/run21.log. Do not duplicate.
- Patch 0016 kernel downloaded/checksum verified: SHA256 `d06336c54baa06e0df6561a8bd7df98a9b9f7502c9381c62d1a44c045cd1ce46`, `xnu/out-0016/kernel.development.sweet`; buildtree/log `research/bt20` / `research/run20.log`. QEMU `research/q0016` (10s) prints platform expert registered, zero unknown glyphs, no panic observed BUT stalls (timer mapping still wrong until 0017). Direct actual-kernel GDB tests passed for registered instance and target/product names, including maxLength=1/0 and NULL, sentinels intact (`research/check-platform.gdb`, `research/check-platform3.log`, `research/q0016-api3`). First two fixture attempts used premature/wrong return breakpoints; discarded, corrected fixture uses start-return observation then a function-entry return trap. No phone boot yet.
- Patch 0016 CI `38750733996` PASSED (4m3s); artifacts not yet downloaded/booted. Patch 0017 prepared to fix QEMU timer fidelity BEFORE interpreting future no-panic/stall runs: actual `qemu-system-aarch64 -M virt,gic-version=3,dumpdtb=research/qemu-virt.dtb ...` timer property has virtual PPI 11 => INTID 27, while phone uses INTID 19. Kernel formerly hardcoded 19. New optional /arm-io/gic virtual-timer-intid property selects the PPI (validate exact u32 and 16..31); missing property retains phone default 19. One RO-late global drives GIC enable and sleh_irq dispatch. tools/mkadt.py emits phone19/QEMU27. Both C TUs compile (`research/timer-objs`), ADT serialize/parse checks confirm values/addresses, all 17 patches apply and match. Sandbox `research/bt19/xnu`, originals `research/pre0017`; initial local sleh compile failed due read-only exported header not updated, fixed export and BOTH recompiled successfully. CI exports normally.
- Architecture correction for next agent: CURRENT SWEET board_config does NOT define USE_APPLEARMSMP; it compiles the legacy IOCPU path, not cpu_boot_thread/IOPMGR AppleARMSMP. Existing roadmap mentions AppleARMSMP as a future approach. Do not look for gCPUIC/gAIC/gPMGR globals in the current kernel: they are absent. Determine the actual CPU-init wait after platform registration before choosing the next driver architecture.
- Patch 0016 committed/pushed as `0dcdf5c`; Actions `38750733996` running: https://github.com/dipa-studio/ios-sweet/actions/runs/38750733996. Next artifacts: `xnu/out-0016`, `research/bt20`, `research/run20.log`.
- Patch 0016 implemented: new `iokit/Kernel/arm/SweetPlatformExpert.cpp`, arm64 source list, built-in personality in KernelConfigTables.cpp guarded by ARM64_BOARD_CONFIG_SWEET. Matches only xiaomi,sweet at score 10000, keeps IOPanicPlatform fallback for other hardware. Uses IODTPlatformExpert base initialization/power domain and createNubs CPU logical-ID annotation; overrides top-level publishing because base requires nonexistent Apple NVRAM /options. Publishes real CPU/top-level nodes, provides bounded target/product names, and registers the platform service. GIC/PMGR and arm-io child-bus drivers remain separate work. Both C++ TUs compile (`research/platform-objs`), all imports resolve against 0015/own objects, all 16 patches apply and sources match. Sandbox `research/bt19/xnu`; originals `research/pre0016`. NEXT: full Actions/QEMU; inspect CPU boot waits after the platform registers, do not equate absence of panic with successful boot.
- Patch 0015 CI `38749549779` PASSED (4m3s), checksum verified. Kernel `xnu/out-0015/kernel.development.sweet`, SHA256 `ca1b424a596c7058550ad1659080a333e7643270f019656401d80bf2b876645f`; buildtree/log `research/bt19` / `research/run19.log`. QEMU `research/q0015` (10s, zero unknown glyphs) passes trust-cache handling and machine lockdown, then panics `Unable to find driver for this platform: "xiaomi,sweet" @IOPlatformExpert.cpp:2319` (IOPanicPlatform fallback). Next concrete work: add a built-in SWEET IODTPlatformExpert personality/class; keep the fallback for mismatched platforms. No phone boot yet.
- Direct trust-cache API checks PASSED in the actual 0015 kernel under QEMU/GDB (`research/check-trustcache.gdb`, `research/check-trustcache.log`, `research/q0015-api`): runtime pointers NULL, hash/UUID queries return NOT_FOUND, typed/untyped/legacy loads return NOT_SUPPORTED, capabilities outputs zero and bounds preserved, invalid-argument cases still reject. Second PRNG check also verifies contended refresh returns immediately (`research/check-prng2.log`, `research/q0014-api2`).
- Patch 0015 committed/pushed `8f851d4`, Actions run `38749549779` running: https://github.com/dipa-studio/ios-sweet/actions/runs/38749549779. Do not duplicate.
- Direct PRNG API checks passed in the REAL 0014 kernel under QEMU/GDB, stopped before trust-cache init (`research/check-prng.gdb`, log `research/check-prng.log`, guest `research/q0014-api`). Invoked read_random for 600 bytes (chunking and buffer sentinel intact), repeated output differed, zero-length read/write succeeded, write_random(64-byte input) succeeded, secondary generator 1 initialized and generated distinct output. QEMU log has no exceptions during these checks; guest was discarded after the tests. This tests API behavior, not a cryptographic certification or SMP scheduling.
- Patch 0015 implemented in `bsd/kern/kern_trustcache.c`: SWEET with missing Image4/AMFI marks both runtime pointers NULL and logs service unavailability. Existing static loader handles NULL. Public load APIs return KERN_NOT_SUPPORTED; hash/UUID queries return KERN_NOT_FOUND; capabilities outputs are zero with KERN_NOT_SUPPORTED. Invalid-input checks remain, and Apple paths/available-provider initialization retain existing behavior. No trust is granted. Local arm64 compile passed (`research/trustcache-objs`), all 15 patches apply and sources match. Sandbox `research/bt18/xnu`, baseline `research/pre0015`. Next: Actions/QEMU and direct unavailable-service API checks using GDB.
- Patch 0014 CI `38748543058` PASSED (4m0s), artifact checksum passed. Kernel `xnu/out-0014/kernel.development.sweet`, SHA256 `741de20c851755d333759d7e668346f3edb4f325efac4cc7c142d80d1f5ff75c`; buildtree/log `research/bt18` / `research/run18.log`. QEMU run `research/q0014` (10s, zero unknown glyphs) prints `SWEET PRNG: HMAC-SHA256 registered`, passes commpage initialization, loads vnguard policy, THEN panics `image4 interface not available @kern_trustcache.c:253`. PC=0 fault FIXED IN QEMU. No new phone boot performed.
- Next work: SWEET currently has neither AppleImage4 nor AMFI. `trust_cache_runtime_init` unconditionally requires both; unlike BCM2837, SWEET enters this startup path. Investigate an explicitly unavailable trust-cache runtime with rejected loads/no query matches while providers are absent; preserve all Apple-platform checks. Merely skipping the startup call would leave later AMFI/IMG4 dereferences unsafe. Existing load_static_trust_cache already handles trust_cache_rt == NULL.
- QEMU runner now supports `--kernel MATCHING_KERNEL --fail-on-panic`: runtime font extraction from the payload produces exact `fb.txt`, plus `result.json`; a visible panic returns 2. Baseline 0013 re-run `research/q0013-text` detects the same NULL callback panic with zero unknown glyphs. Synthetic glyph roundtrip (including literal `?`) and invalid-VA rejection passed. Screen rows remain circular, not a full log; `no_panic_observed` does not prove successful boot. README has the invocation.
- Patch 0014 committed/pushed as `be61f99`; Actions run `38748543058` is running: https://github.com/dipa-studio/ios-sweet/actions/runs/38748543058. Do not start a duplicate build.
- 2026-10-15 active: hardware photo `IMG_20261015_160033.jpg` confirms the same PC=0 instruction-fetch abort as QEMU (LR `0xfffffe0007443fe4` = read_random). Independently reproduced with kernel 0013 in `research/q0013` (8-second run); prior allocator fix works. Latest tooling commit reviewed: `8d66d2d`.
- Patch 0014 implemented: registers a built-in HMAC-SHA256 DRBG provider at EARLY_BOOT/FIRST, before commpage creation. A shared mutex serializes output/reseed across CPUs (correctness first; no per-CPU scaling yet), initgen validates CPU identities, refresh uses try-lock and the existing entropy filter/health tests. SWEET entropy accumulation and write_random now use the built-in SHA256 directly; no incomplete g_crypto_funcs table is registered. The existing bootloader-seed requirement remains enforced.
- Local validation: both PRNG translation units compile to arm64 objects in `research/prng-objs`; no SHA256 wrapper imports remain. All 14 patches apply in CI order and resulting PRNG sources match local inputs. Sandbox `research/bt17/xnu`, originals `research/pre0014`, checker `research/check-patch-order.py`. NEXT: Actions kernel build, download to `xnu/out-0014`, run the QEMU payload before requesting another phone boot. Baseline QEMU ADT/payload are `research/q0013.adt` / `research/q0013.bin`.
- 2026-10-15 08:15 QEMU EMULATION WORKS (`tools/qemu-sweet.py`, see "QEMU emulation" below). Patch 0013 VERIFIED in QEMU: boot passes kmem_crypto_init and ulock_initialize. NEXT BLOCKER (reproduced in QEMU, expected on hardware too): `pmap_create_commpages -> random() -> RandomULong -> read_random -> prng_funcs.refresh == NULL` (instruction fetch abort at pc 0, lr read_random+0x34). Nobody calls `register_and_init_prng` (closed corecrypto kext does it on Apple). Fix = patch 0014: SWEET-registered cckprng_funcs provider (osfmk/prng/prng_random.c); note random_cpu_init also panics while !prng_ready. The 07:26 hardware boot of 0013 will most likely show this same panic.
- Previous hardware boot: patch 0013 (`a3a46e2`) CI `38686345689` PASSED (3m44s), image `shim/out/shim-boot-0013.img` was temporarily booted on 2026-10-15 at 05:26 UTC: send OKAY (0.407s), boot OKAY (0.134s), total 0.546s. Superseded by the hardware/QEMU PC=0 observation and patch 0014 work above.
- Latest kernel implementation commit: `8f851d4`. Latest successful kernel CI: `38749549779` (patch 0015), https://github.com/dipa-studio/ios-sweet/actions/runs/38749549779.
- CURRENT image: `shim/out/shim-boot-0013.img`, 16,412,672 bytes, SHA256 `110d1c46aee4b60253db24b6535e9781c4cf0fd25ca2a70e9016cf81378cea25`. Kernel `xnu/out-0013/kernel.development.sweet`, SHA256 `f0b1d375995316c550ce7014ff234d8271e9b9ac2857795ceff41b76ba2cd740`, plain arm64. CI log confirms 0013 applied; checksums passed. Entry PA `0xb146b488`, kernel end PA `0xb1ff4000`.
- Current log/buildtree: `research/run17.log`, `research/bt17` (extracted, no nested .git; preferred baseline for patch 0014). Payload/ADT: `shim/out/payload-0013.bin`, `shim/out/sweet-0013.adt` (includes /defaults). Unpacked ramdisk byte-compared with payload in `shim/out/check-0013`.
- Patch-0012 original image: `shim/out/shim-boot-0012.img`, 16,412,672 bytes, SHA256 `cf4b8ec4a65a51888d71c17d09cd0831d6b761b90e2531fdb8e575c32f4fd24a`. Kernel `xnu/out-0012/kernel.development.sweet`, SHA256 `69b78f49f6459cc63a0af44e7cf73c1779ef72130ef72b12ae86c88183ffc742`; checksums passed and sweet_early_relocate symbol present. Entry PA remains `0xb146b488`, kernel end PA `0xb1ff4000`.
- Patch-0012 log/tree: `research/run16.log`, `research/bt16` (extracted, no nested .git; preferred baseline for next patch). CI log confirms 0012 applied. Payload `shim/out/payload-0012.bin`, ADT `shim/out/sweet-0012.adt`; packed ramdisk byte-compared with payload in `shim/out/check-0012`.
- Active payload command line: `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam`.
- Latest observed photo: `/home/dipa/Downloads/IMG_20261015_071346.jpg` (local only). Stages 9 (kernel framebuffer VA), 10 (TTBR0 off/TLB flush), 11 (physmap table pointers) and 12 (calling pmap_bootstrap) all visible. Persistent diagnostics are VERIFIED ON HARDWARE.
- pmap.c:1987 is the assert after SecureDTLookupEntry(NULL, "/defaults", &entry). The pmap-max-asids property itself is optional; an empty /defaults uses XNU's MAX_ASIDS. Do not bypass the assertion or guess an ASID limit.
- Previous payload-only fix image: `shim/out/shim-boot-0012-defaults.img`, SHA256 `3ff61a268853c6ea7cbf284833f1346a1d791980ca366b6e1a215a86e040512b` (16,412,672 bytes). Same kernel/CI as 0012; ADT 2516 bytes (`shim/out/sweet-0012-defaults.adt`), root /defaults present with no ASID override. Payload `shim/out/payload-0012-defaults.bin`; unpacked ramdisk exactly matches payload (`shim/out/check-0012-defaults`). This boot reached the allocator crypto NULL-pointer fault below.
- Latest hardware photo: `/home/dipa/Downloads/IMG_20261015_071608.jpg` (local only). Normal serial framebuffer console works. VM bootstrap/zone initialization, logging, scheduler, IPC, KDP and ulock_initialize print before the kmem_crypto_init data abort. gPanicBase warnings are expected because PLATFORM_PANIC_LOG_DISABLED is configured, and are not the faulting call.
- Faulting instruction: `ldr x0, [x8, #0x150]` in crypto_random_kmem_ctx_size; x8 = NULL g_crypto_funcs. Source: libkern/crypto/corecrypto_rand.c and osfmk/vm/vm_kern.c:2448. rand.h explicitly permits NON-cryptographic kmem RNGs, but requires no locking in generation and context size <=256. Do not substitute read_random here: the global PRNG provider is also unregistered and kmem generation must remain lock-free. early_random may seed contexts during single-CPU bootstrap; it uses the built-in HMAC-SHA256 DRBG with the host-generated ADT seed.
- Patch 0013 keeps the replacement entirely private to vm_kern.c: SplitMix64 state is 8 bytes per CPU/sizeclass, initialized using early_random. Uniform selection uses rejection sampling to avoid modulo bias. Other platforms retain the original corecrypto calls; SWEET's generic crypto APIs/registration remain untouched. Reference: https://prng.di.unimi.it/splitmix64.c (Sebastiano Vigna, public domain).
- Validation: vm_kern.c compiles to arm64 (`research/kmem-rng-objs`, using `research/run16.log` + `research/bt16`). Undefined symbols include early_random and NO crypto_random_* references. All 13 patches apply in CI order and match the checked source. Original saved as `research/vm_kern.pre0013.c`.
- RNG host checks: extracted actual new helper in `research/kmem-rng-check.c`; 16,384 outputs match the author's downloaded reference (`research/splitmix64-reference.c`), 147,456 bounded outputs satisfy their limits (including 1 and UINT64_MAX), and a forced rejection advances correctly. Full kernel crypto services and the registered kernel PRNG provider remain future work; this patch only supplies the allocator RNG contract.
- Patch-0011 artifacts: kernel `xnu/out-0011/kernel.development.sweet`, log `research/run15.log`, extracted buildtree `research/bt15` (no nested .git). CI log confirms patch 0011 applied; artifact checksums passed. Kernel is plain arm64, entry VA `0xfffffe000746b488` -> PA `0xb146b488`, kernel end PA still `0xb1ff4000`.
- Patch-0011 image: 16,412,672 bytes, SHA256 `29ad5cbfc752473de4af2c471a24a8b34f92da4bff5afec65c614b90cd3ad53a`. Kernel SHA256 `8ac3a956894b09f7bd0d890de8c5de38c65513287feb1e1bcd12db8ca772ee5c`. Payload/ADT in `shim/out/payload-0011.bin`, `shim/out/sweet-0011.adt`. Header unpacked/checked in `shim/out/check-0011`.
- First patch-0011 attempt timed out waiting for FASTBOOT; superseded by the successful send/boot recorded above. Do not repeat the boot before the user reports the current screen.
- Patch-0011 photo: `/home/dipa/Downloads/IMG_20261015_070218.jpg` (local only). Visible stages 1-8 and old red shim text; entry PA B146B488, boot args PA B1FF8000 / top B1FFC000. No evidence yet locating execution after stage 8.
- Patch-0012 sandbox: `research/bt15/xnu` (NO nested .git). Proposed permanent debug VA: VM_MIN_KERNEL_ADDRESS + ARM_TT_L2_SIZE, in the reserved gap below gVirtBase and above low globals; map the same PA framebuffer as Normal-NC in the final TTBR1 before arm_vm_physmap_init changes phystokv(). Keep the early panic sink until normal kprintf startup.
- Patch 0012 validation: all 4 changed C translation units compiled to arm64 objects in `research/post-ttbr0-objs`; internal header export added to osfmk/arm64/Makefile and recreated locally in EXPORT_HDRS for pe_serial.c. All 12 patches apply in CI order and match all 6 edited files. Baselines are saved under `research/pre0012/`.
- Actual mapping-helper host model (`research/early-map-check.c`, barrier stubbed only for host compilation) confirms: VA `0xfffffe0002000000`, L1 index 2016, L2 index 1; descriptor `0x006000009c000605` = framebuffer PA, Normal-NC, outer-shareable, EL1 RW, PXN+UXN. All other L2 entries preserved. The gap is excluded by ARM_LARGE_MEMORY's pmap_virtual_region(0).
- Next boot command line MUST add `-enable_kprintf_spam`: DEVELOPMENT disables kprintf_output by default even when DB_KPRT is set (`pexpert/arm/pe_kprintf.c`). Planned command line: `debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam`.

### Patch 0012 screen legend (after original stages 1-8)
- 9: kernel framebuffer VA live; 10: TTBR0 off / TLB flushed; 11: physmap pointers rebound.
- 12: calling pmap_bootstrap; 13: pmap_bootstrap returned; 14: early I/O page tables ready; 15: arm_vm_init returned.
- 16: calling serial_init; 17: fbcon config read / mapping; 18: fbcon mapping returned; 19: serial_init returned; 20: kprintf startup complete.
- Normal serial initialization clears the screen, so earlier stages may disappear before 19. If serial exists, early console disables after 20; otherwise early panic output stays enabled.
- Local source sandbox: `research/bt13/xnu` (git baseline `623b060`, patches 0001-0008 already in baseline; uncommitted export edits correspond to patch 0009). Preserve those edits when producing patch 0010.
- Previous compile log/tree: `research/run13.log`, `research/bt13`; previous kernel: `xnu/out/kernel.development.sweet`. New successful log/tree: `research/run14.log`, `research/bt14`; new kernel: `xnu/out-0010/kernel.development.sweet`.
- Current device access: patch-0012 image was temporarily started from FASTBOOT; no adb connection is expected. Never flash partitions for bring-up; use `fastboot boot` with the shim image.
- Patch `0010` lives in `xnu/patches/0010-sweet-early-framebuffer-debug.patch`. Its exact compiled sources are under `research/bt13/xnu`; generated objects are in `research/early-objs`.
- Local checks passed: all 5 changed/new translation units compiled to arm64 Mach-O objects; new console only imports `_doprnt` and `iso_font` (both exist in the previous kernel). All 10 patches applied to a temporary upstream git index in CI order, and all 7 resulting changed source files matched the local compile inputs.
- Reproduce changed-file compilation: `XNU_CC_OBJDIR="$PWD/research/early-objs" python3 tools/xnu-cc.py research/run13.log research/bt13 arm_init.c arm_vm_init.c kern/debug.c arm64/start.s osfmk/arm64/sweet_early.c --per-file` (create the output directory first). The helper now accepts explicit new source paths absent from the old artifact's Makefiles and rejects empty selections.
- Device command result: `adb reboot bootloader`, then `timeout 30s fastboot boot shim/out/shim-boot-0010.img`: send OKAY (16028 KB), boot OKAY, total 0.541s. This confirms bootloader acceptance only, not XNU progress.
- The successful boot above was on 2026-10-14. On 2026-10-15, `timeout 45s fastboot boot ...` timed out waiting for a device; no image was sent. Boot image SHA256 rechecked and unchanged. No new build is needed to repeat the trial.
- Earlier retry: `timeout 90s fastboot boot shim/out/shim-boot-0010.img` timed out (exit 124). This is superseded by the successful 2026-10-15 repeat boot recorded above; do not repeat it until the user reports the screen.
- User observation confirms MMU-on C entry, platform expert, CPU topology/bootstrap, startup bootstrap and cpu_init all complete. Failure is within arm_vm_physmap_init (non-XNU_MONITOR branch), before stage 8 / TTBR0 teardown. Old red shim text remains behind the early console; this is expected because only written rows are cleared.
- New edit/compile sandbox is `research/bt14/xnu` (extracted artifact, NO nested .git; `git diff` there would act on the parent project). Use a saved baseline file / generated unified diff, or explicitly initialize a local baseline before editing. `research/bt13/xnu` does have its own baseline git repo.
- Exact user-reported panic: `arm_vm_init.c:1606 Assertion failed: (temp_ptov_table[ptov_index - 1].va + temp_ptov_table[ptov_index - 1].len) <= physmap_end`.
- Root cause: MacOSX build sets `ARM_LARGE_MEMORY=1`; `XNU_MONITOR` is off. Block alignment in arm_vm_physmap_slide can add gaps, but the non-PPL large-memory physmap_end omitted the padding already present in the small-memory branch. Patch 0011 reserves PTOV_TABLE_SIZE*ARM_TT_TWIG_SIZE (8*32MB) in both the aperture bound and its L1 allocation; monitor/PPL builds retain zero padding. The assertion is preserved.
- Validation: `arm_vm_init.c` compiled to an arm64 object using `research/run14.log` + `research/bt14` (object in `research/physmap-objs`). All 11 patches apply in CI order and produce exactly that source. Host harness `research/physmap-check.c` extracts the real XNU slide function with page-table-write stubs: all 65,536 page-aligned slides within 1GB checked against the current kernel memory layout; old bound fails 65,504 cases, padded bound fails zero; worst overrun 0x1ffc000 bytes. Baseline source saved as `research/arm_vm_init.pre0011.c`.
- CI: https://github.com/dipa-studio/ios-sweet/actions/runs/38638702083 (success; artifacts and full log downloaded).
- New image: `shim/out/shim-boot-0010.img` (16,412,672 bytes, SHA256 `ed2c270e0081bad5ff517be415f9bfe8805463aea7866732203fa97ac737e64e`). Previous image preserved as `shim/out/shim-boot-pre0010.img`.
- New kernel SHA256: `3ee05270cc291c49589a101678f904b1b69cb2f2575e05398bce8a4e052f409a`. Artifact checksums passed; arm64 (not arm64e), early-console symbols present. Payload entry VA `0xfffffe000746a488` -> PA `0xb146a488`; kernel end PA `0xb1ff4000`. Command line remains `debug=0xc serial=3 cpus=1 keepsyms=1`.

### Patch 0010 screen legend
- Bottom-left white 64x32 tile: assembly entered `start_first_cpu`, before page tables/MMU.
- Text `SWEET EARLY 1`: entered `arm_init` with MMU on; 2: platform expert done; 3: CPU topology done; 4: CPU bootstrap done; 5: startup bootstrap done; 6: CPU init done; 7: entering `arm_vm_init`; 8: leaving bootstrap TTBR0.
- `SWEET EARLY PANIC:` prints a copy of panic arguments before normal panic handling needs per-CPU/thread state.
- In patches 0010/0011 the console disabled before TTBR0 invalidation; patch 0012 replaces that with a persistent kernel VA alias and diagnostics through normal serial/kprintf startup (see its legend above).
- Screen contents require a physical observation from the user: adb cannot capture a framebuffer after Android has been replaced by a bare-metal kernel.

## QEMU emulation (2026-10-15)
- `tools/mkadt.py OUT.adt --qemu` = same ADT with QEMU virt GICD 0x08000000 / GICR 0x080a0000 / CNTFRQ 62.5 MHz.
- `tools/qemu-sweet.py PAYLOAD OUTDIR [--secs N] [--hmp "x /8gx ADDR"] [--gdb]`: host-side copy of shim/loader.c
  (segments + ADT + boot_args at their PAs, 6-instruction stub sets x0 and jumps), `-M virt,gic-version=3 -cpu cortex-a76`
  (same ID_AA64MMFR0 as Kryo 470), 4G RAM so 0x9c000000 fb and 0xb0000000 kernel are plain RAM.
  Outputs `fb.png` (the screen), `regs.txt`, `qemu.log` (exceptions with ESR/FAR/ELR). `--gdb` halts with a stub on :1234
  (system `gdb` + `set architecture aarch64`, `hbreak *VA` works with the MMU on).
- Not emulated: Qualcomm XPUs, ABL, real panel, PSCI conduit (QEMU virt uses hvc). Hardware still decides those.
- Example: `python3 tools/mkadt.py /tmp/q.adt --qemu && python3 tools/mkpayload.py xnu/out-0013/kernel.development.sweet /tmp/q.adt /tmp/pq.bin --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 -enable_kprintf_spam' && python3 tools/qemu-sweet.py /tmp/pq.bin /tmp/q`

## Layout
- `backup/`  partition dumps + full sdb..sdf LUNs, `SHA256SUMS` (all verified on-device == local)
- `dtb/`     live FDT (from OrangeFox), PixelOS boot.img dtb, dtbo, dts versions
- `shim/`    bare-metal boot shim (`./build.sh` -> `out/shim-boot.img`)
- `research/` mainline sm7150 dts, XNU pe_fiq.c etc, PocketDarwin patches
- `tools/unpack_bootv2.py`

## Device access
- No root in PixelOS 17 (user build, adb root disabled). Root = `fastboot boot recovery/recovery.img` (OrangeFox R12.0_2, temp boot, not flashed).
- `fastboot boot` works for stock boot.img, OrangeFox, and our shim.
- Shim resets itself via PSCI SYSTEM_RESET; IMEM restart_reason 0x77665500 is seen
  (bootreason=bootloader) but ABL still boots Android, so it does not stop in fastboot.

## Hardware facts (live DT + shim measurements)
- Framebuffer (ABL cont splash): 0x9c000000, 1080x2400, stride 4320, a8r8g8b8, panel is DSC *video* mode (no kick needed).
- RAM: 0x80000000+2G, 0x100000000+0xfcc00000. Live reserved-memory = PixelOS base dtb + sweet dtbo fixups.
- GICv3: GICD 0x17a00000, GICR 0x17a60000 (stride 0x20000, 8 PEs). GICD_TYPER 0x007B041D: SecurityExtn=1 (DS=0).
  ABL leaves GICD_CTLR = 0x10 (ARE_NS on, groups off). Writing 0x13 reads back 0x12: EnableGrp1A (bit1) is the one that matters.
- GIC Group 1 VERIFIED (shim 2c): vtimer INTID 19 = 30 IRQs/300ms, ptimer INTID 18 = 30, SGI 1 to self = 10/10.
  ICC_SRE=1 already, ICC_CTLR 0x8440, priority/enable writes stick. GICR_IGROUPR0 and GICD_IGROUPRn read 0 from NS
  even though Group 1 delivery works (RAZ in NS view) - do not use them as a health check.
  INTID 28 shows pending constantly (unknown PPI 12).
- Timer 19.2 MHz. Qualcomm PPI numbering: secure-phys/phys/virt/hyp = PPI 1/2/3/0 -> vtimer INTID 19, ptimer 18 (NOT 27/30).
- PSCI 1.1 via SMC, SMCCC 1.1. CPU_ON ok; SYSTEM_SUSPEND and SYSTEM_RESET2 unsupported.
- SMP: CPU_ON works for all 7 secondaries (MPIDR 0x81000n00).
- Entry EL1, MMU off, x0 = DTB (magic ok). ID_AA64MMFR0 = ...101122: 16K granule YES, 4K YES.
- No serial console: debug UART SE is in SPI mode on sweet. Screen is the only debug channel.

## XNU implications
- XNU GICv3 path (pexpert/arm/pe_fiq.c, VMAPPLE) puts the timer (PPI 27) in Group 0 = FIQ. With SecurityExtn=1,
  Group 0 is secure -> impossible from NS EL1. Need: Group 1 IRQ, ICC_IGRPEN1, INTID 19, and the timer handled in the IRQ path.
- XNU VMAPPLE PSCI uses `hvc`; sweet needs `smc`.
- PocketDarwin NonAppleGenericCpu.patch only swaps -mcpu=apple-h7 for -march=armv8-a.

## Milestone 3 (2026-10-14): 16K MMU verified
- ABL loads the Image at 0x80080000 (base 0x80000000 + 0x80000) regardless of text_offset=0.
- 16K granule, T0SZ=28 (L2 start, 32MB blocks), MAIR 0x44ff00, TCR 0x1409CB51C, PARange=2 (40-bit PA).
- Only own image pages WB (Qualcomm XPUs: never map carve-outs cacheable); fb Normal-NC; rest Device.
- AT S1E1R checks all as expected; unmapped -> translation fault L2.
- Fill fb: 110 ms (MMU off) -> 3.5 ms (NC). Memory loop 70 ms -> 0.7 ms. vtimer + SGI IRQs still fine with MMU on.

## XNU loader design notes (xnu-8796)
- Entry: `_start` -> `start_first_cpu`, x0 = boot_args (PA), MMU off, EL1. boot_args: Revision/Version 2,
  virtBase/physBase/memSize/topOfKernelData, Boot_Video (fb 0x9c000000, rowBytes 4320, 1080x2400, depth 32),
  deviceTreeP/Length (ADT), CommandLine[608], bootFlags, memSizeActual.
- start.s maps [kernel mach header .. physBase+memSize) with cacheable BLOCKS. On Qualcomm every XPU carve-out
  must therefore lie below the kernel: use physBase = 0xb0000000 (above ramdump_fb 0xaf000000+16M,
  cdsp_sec, qseecom), end before rmtfs 0xfde01000 -> memSize ~0x4d000000 for bring-up.
- Base ADT: iPhone 8 d20ap DeviceTree from iOS 16.7.16 IPSW (tools/remotezip.py + tools/adt.py; LZFSE payload).
  Template values are empty: the loader must fill chosen/*, memory-map, cpu*/reg etc. like iBoot.
- XNU build: GitHub Actions (dipa-studio/ios-sweet, xnu.yml) via blacktop/darwin-xnu-build, MACOS_VERSION=13.5.
  New-clang fixes in xnu/patches; Xcode 16 iig is incompatible (IORPC.kernelContent) -> macos-14 runner, oldest Xcode.

## Kernel architecture plan (SWEET machine config)
- First stock build (VMAPPLE) is arm64e (PAC). Kryo 470 = ARMv8.2, no PAC -> SWEET must build plain arm64
  (HAS_APPLE_PAC is only defined for __arm64e__, so arm64 drops PAC automatically).
- Template: VMAPPLE (16K pages, GICv3 code in pe_fiq.c) minus APPLEVIRTUALPLATFORM hypercalls/paravirt PAC,
  plus BCM2837 (generic ARM: no APPLE_ARM64_ARCH_FAMILY Apple sysregs). Do not link the KDK lib<cfg>.os.*.a.
- Timer: XNU expects it on FIQ (sleh_fiq -> rtclock_intr). SWEET: GIC Group 1, so sleh_irq must ack ICC_IAR1,
  route INTID 19 to the same timer path, SGIs to cpu_signal_handler, EOI via ICC_EOIR1.
- SMP/interrupts: open-source AppleARMSMP (USE_APPLEARMSMP) needs an IOInterruptController (AIC role) and an
  IOPMGR (CPU power -> PSCI CPU_ON via SMC). Plan: built-in IOKit classes (SweetGIC, SweetPMGR, platform
  expert) compiled into the kernel and matched via iokit/KernelConfigTables.cpp -> no closed kexts needed.
- Debug: xnu patch 0003 sweet-fbcon = kprintf drawn on the framebuffer (ADT node "sweet-fbcon").
- ADT /arm-io ranges must not map to parent 0 (pe_arm_get_soc_base_phys()==0 disables serial): use parent 0x1000000.

## Status 2026-10-14 late: first XNU boot attempt
- SWEET kernel builds (CI run 38635343732, patches 0001-0009). Verified offline: arm64, no PAC, no >v8.2 code,
  70300 classic relocs all in range (slide 0 = no loader work), entry = __start.
- Boot: shim loader worked (initrd 0x846F0000-0x855FE3E0, BOOTARG 0xB1FF8000, JUMP 0xB146A488) but screen stayed
  red -> xnu died/hung before serial_init (fbcon). kprintf is a no-op before STARTUP_SUB_KPRINTF.
- Historical next-step plan (patch 0010, now implemented and built; see active handoff above): early fb debug channel
  1. start.s start_first_cpu (after mov x20,x0 / mov x21,#0): PA marker band (MMU off, direct store to 0x9c000000).
  2. start.s after both create_bootstrap_mapping calls: add ONE block entry to the existing V=P L2 table
     (x25 + 2*PGBYTES; do NOT call create_bootstrap_mapping again: fb and RAM share L1 entry 0, it would replace
     the L2 table). Entry = TYPE_BLOCK|VALID|SH(OUTER)|ATTRINDX(CACHE_ATTRINDX_WRITECOMB=1)|AF|PNX|NX|0x9c000000,
     index = (va & ARM_TT_L2_INDEX_MASK) >> ARM_TT_L2_SHIFT.
  3. new osfmk/arm64/sweet_early.c (+files.arm64 entry): sweet_early_mark(n) colored bands at screen bottom and
     sweet_early_putc using iso_font via VA=PA while the bootstrap TTBR0 is live; disable before
     arm_vm_init's set_mmu_ttb(invalid_ttep) (arm_vm_init.c:2148).
  4. markers in arm_init after PE_init_platform, ml_parse_cpu_topology, cpu_bootstrap, kernel_startup_bootstrap,
     cpu_init, before arm_vm_init.
  5. panic hook at top of panic_trap_to_debugger: va_copy + _doprnt(fmt, &copy, sweet_early_putc, 16).
  Verify with tools/xnu-cc.py (compile+symbols) and patch-order simulation before the single CI run.

**AGENT review fixes (2026-10-21, user review c68e324 + this pass):** (1) /run t= deadline now
SIGKILLs the program (regression from the workflow-fix pass - the reap loop skipped its kill when
timed_out was already set; sleep 20 t=2 returns in 2.0 s, program gone); (2) /run polls the client
socket too (recv MSG_PEEK): a vanished peer is noticed within one poll even while the program is
silent (killed curl + sleep 15 t=0 -> /ping answers in 0.0 s, child killed); (3) background jobs:
/start?name=N&a=...[&log=PATH|console], /jobs, /stop[&sig=9] - state is /tmp/kunzite-agent pid
files (survives agent restarts; children hold only their log fd via CLOEXEC; console = live USB
log); (4) Expect: 100-continue answered (4 MiB upload 0.009 s, was ~1 s stall); (5) request log
no longer doubles the slash; (6) 0910 regenerated with the comment kept line-aligned (applies to
exactly one comment block; series re-verified, 151).
