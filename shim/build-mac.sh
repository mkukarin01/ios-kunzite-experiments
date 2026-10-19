#!/bin/bash
# macOS build of the shim and a boot.img v4: same output as build.sh, but with
# brew's aarch64-elf-gcc (GNU cross, the compiler family the Linux build and CI
# use), BSD cpio, and AOSP mkbootimg.py fetched to temp/ios-m8/tools/.
# Diagnostics image when called with no argument; with a payload file as $1 it
# is appended behind the 4K-aligned image end exactly like build.sh does.
set -euo pipefail
cd "$(dirname "$0")"
PREFIX=aarch64-elf
CC=$PREFIX-gcc
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
MKBOOTIMG=${MKBOOTIMG:-$HOME/Work/rtk-tablet/temp/ios-m8/tools/mkbootimg.py}
[ -f "$MKBOOTIMG" ] || { echo "error: mkbootimg.py not found (set MKBOOTIMG)" >&2; exit 1; }
mkdir -p out
for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c; do
	$CC $CFLAGS -c $f -o out/${f%.*}.o
done
# The image runs wherever ABL puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$($PREFIX-readelf -r out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,-N \
	out/start.o out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o -o out/shim.elf
$PREFIX-objcopy -O binary out/shim.elf out/Image
# Pad to _end so .bss/stacks exist as zeroed bytes (no truncate(1) on macOS).
size=$((16#$($PREFIX-nm out/shim.elf | awk '$3=="_end"{print $1}')))
python3 -c 'import os,sys; open(sys.argv[2],"ab").write(b"\0"*max(0,int(sys.argv[1])-os.path.getsize(sys.argv[2])))' "$size" out/Image
if [ -n "${1:-}" ]; then
	# topaz ABL drops the v4 boot ramdisk: append the payload at the first 4K
	# boundary after _end (loader.c) and grow the image_size field at offset 16.
	python3 - "$size" out/Image "$1" <<'EOF'
import struct, sys
size, path, payload = int(sys.argv[1]), sys.argv[2], sys.argv[3]
with open(path, "r+b") as f:
    f.seek((size + 0xfff) & ~0xfff)
    f.write(open(payload, "rb").read())
    f.seek(0, 2)
    total = f.tell()
    f.seek(16)
    f.write(struct.pack("<Q", total))
EOF
fi
(cd out && rm -rf rd && mkdir rd && cd rd && find . | cpio -o -H newc --quiet | gzip -9 > ../ramdisk.bin)
python3 "$MKBOOTIMG" --header_version 4 --kernel out/Image --ramdisk out/ramdisk.bin \
	--os_version 13.0.0 --os_patch_level 2025-01 \
	--cmdline "console=null" -o out/shim-boot.img
ls -l out/Image out/shim-boot.img
