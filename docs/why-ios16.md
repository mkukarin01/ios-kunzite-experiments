# Why iOS 16 — and not something newer

Decision record for the kunzite port (and for the sweet/topaz lineage it inherits).
Written 2026-10-20, while the phone was booting iOS 16.7 kernels; update it if the
target ever moves. Facts are grounded in this repo's patches/tools; the assessments
are the port's, not Apple's.

## The short version

The hard problem in this project is **the kernel on foreign silicon**, not the iOS
version. Every ounce of difficulty we want to spend goes into XNU-on-SM6475; the
userland is a payload that must merely run. iOS 16.7 (20H392) with the iPhone X
(D22AP) root is the vintage that (a) the whole lineage's machinery already targets,
(b) has the softest hardware/security expectations we can find, and (c) matches the
CPU class story below. Newer iOS buys nothing until the machine underneath is real.

## The reasons

### 1. Lineage economics: 130+ patches and a root pipeline target xnu-8796

sweet ported XNU `xnu-8796` (the iOS 16 generation) to SM7150 and built the entire
bring-up stack on it: patches 0001–0081 (AMFI/sandbox stubs, HFS+, IOSurface,
IOMobileFramebuffer, the IOHIDFamily-1915 user client, activation, diagnostics).
topaz inherited all of it (0080–0126) and kunzite inherits topaz (0127+). Every
patch, the `mkiosroot.sh`/`sweet-root-tweaks.py` root-image pipeline, the QEMU
harness and the tooling encode xnu-8796 assumptions. Moving to iOS 17/18/26 means
re-porting that series onto `xnu-10063+` — new IORPC/IIG drift, new kext ABI
changes — while gaining nothing for bring-up. Re-aim the version *after* the
drivers exist, not before.

### 2. Toolchain pinning works

`xnu-8796` builds cleanly with blacktop/darwin-xnu-build on Xcode 15 runners
(free CI). The topaz README records the constraint: newer Xcode's `iig` does not
match the xnu-8796 IORPC ABI. Newer xnu brings newer generated-code requirements
and a fresh toolchain fight on top of everything else.

### 3. iOS 16.7 is the last iOS for the A11 — which is exactly the guest we want

The root filesystem we run is the **iPhone X (iPhone10,3 / D22AP)** userland, and
A11 devices end at iOS 16 — so 16.7.16 is the *newest* userland that still targets
2017-vintage hardware expectations:

- **ISA floor.** The A11 userland is built for ARMv8.2-class cores (LSE atomics,
  no PAC in the slices we run). kunzite's Cortex-A78/A55 satisfy that natively —
  no emulation. (Contrast: topaz's ARMv8.0 SM6225 needed an LSE/LDAPR trap-emulation
  layer, patch 0081. A newer userland compiled with a higher ISA floor would put
  every device back in that position.)
- **CPU class is already over-qualified.** A 2.4 GHz A78 has roughly A12/A13-era
  single-thread throughput — and comfortably beats the A11 Monsoon @2.39 GHz the
  userland was designed for. Whatever slowness we see (≈3-minute SpringBoard boots)
  comes from having **no GPU** (CPU-copied frames), one enabled core during early
  bring-up (`cpus=1`), and no cpufreq driver — not from CPU vintage. A newer iOS
  would raise the compute floor while the real bottleneck stays unaccelerated.
- **Identity plumbing is proven.** iPhone X = the D22AP gesture-UI identity the
  ADT fabricates (`home-button-type 2`, `/product` from the real D22AP tree,
  MobileGestalt cache, Hactivation without Apple servers — sweet patches 0059+).

### 4. Softer security/SEP expectations

The 16.7 daemons tolerate the machine we can actually offer: permissive AMFI and
Sandbox policies, a stub AppleKeyStore, no SEP, no data protection. That tolerance
is what got sweet from first pixels to SpringBoard. Newer userlands (all-A12+ and
later, Face-ID-era, cryptex/sealed-root-era) lean harder on SEP-backed services and
verified-boot structures we do not have and cannot fake as cheaply — they would
convert driver bring-up work into jailbreak-style trust-chain work.

### 5. Root-image pipeline simplicity

iOS 16.7's OS/SystemOS/AppOS dmgs fold into one journaled HFS+ volume with the
existing scripts; the launchd/daemon accommodations (Setup skip, activation seeding,
watchdog exceptions) are all solved and encoded in `sweet-root-tweaks.py`. Newer
layouts mean new cryptex dance, new first-boot stages, and a new wall list.

## What "newer" would cost, concretely

Re-port ~130 patches to a new xnu; re-fight the toolchain; rebuild the root pipeline
against a new volume/cryptex structure; re-solve daemon bring-up against harder
security expectations; re-verify the ISA floor per device — all while touch, GPU,
audio and Wi-Fi are still open on the hardware side. That is a second port stacked
on top of an unfinished first one, with no user-visible gain until the drivers land.

## When the version could move

Once the machine is boring — GPU-accelerated UI, working touch/Wi-Fi/audio, all
cores — bumping the target is a self-contained project on a proven base (pick the
newest xnu whose toolchain cooperates, port the series, rebuild the root). Until
then: 16.7.16 on the A11's terms, which the A78s find comfortable.
