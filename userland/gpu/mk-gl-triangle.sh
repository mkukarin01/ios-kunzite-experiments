#!/bin/bash
# gl-triangle for the phone (arm64 iOS 16), signed ad hoc as the agent is (agent/build.sh). It loads
# libOSMesa.8.dylib (userland/mesa/mk-mesa-mac.sh) at run time, so it needs no Mesa to build:
#   userland/gpu/mk-gl-triangle.sh   ->   userland/gpu/out/gl-triangle, out/kjoin, out/kfind, out/kdmesg, out/kshot, out/kcsops, out/ktcc(-plain)
# Onto the phone and run, over the agent (the GPU kept: kgpu_keep=1 kgpu>=21). The agent holds a /put
# body in memory and jetsam kills it near 5 MB, so the 15 MB library goes in 2 MB parts, joined by kjoin
# (it prints the size and FNV-1a 64 hash, to compare with the Mac's copy):
#   A=http://192.168.7.2:8022
#   curl -s -T userland/gpu/out/kjoin "$A/put?p=/tmp/kjoin&m=755"
#   curl -s -T userland/gpu/out/gl-triangle "$A/put?p=/tmp/gl-triangle&m=755"
#   split -b 2m $MESA_BUILD/out/libOSMesa.8.dylib parts/osmesa.
#   for f in parts/osmesa.*; do curl -s -T $f "$A/put?p=/tmp/${f#parts/}&m=644"; done
#   curl -s "$A/run?a=/tmp/kjoin&a=/tmp/libOSMesa.8.dylib&a=/tmp/osmesa.aa&a=..."
#   curl -s "$A/run?a=/tmp/gl-triangle&t=90"            (then &a=soft: softpipe, to compare)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
for p in gl_triangle:gl-triangle kjoin:kjoin kfind:kfind kdmesg:kdmesg kcsops:kcsops; do
	clang -O2 -g -Wall -Wextra -std=c11 -target arm64-apple-ios16.0 -isysroot "$SDK" ${p%:*}.c -o out/${p#*:}
	if command -v ldid >/dev/null; then
		ldid -S../../agent/entitlements.plist out/${p#*:}
	else
		codesign -f -s - --entitlements ../../agent/entitlements.plist --generate-entitlement-der out/${p#*:}
	fi
done
# kshot needs more frameworks (it writes PNG files with ImageIO)
clang -O2 -g -Wall -Wextra -std=c11 -target arm64-apple-ios16.0 -isysroot "$SDK" kshot.c -framework CoreFoundation \
	-framework CoreGraphics -framework IOSurface -framework ImageIO -o out/kshot
if command -v ldid >/dev/null; then
	ldid -S../../agent/entitlements.plist out/kshot
else
	codesign -f -s - --entitlements ../../agent/entitlements.plist --generate-entitlement-der out/kshot
fi
# ktcc twice: signed with com.apple.private.tcc.allow = kTCCServicePhotos (as assetsd is), and without
clang -O2 -g -Wall -Wextra -std=c11 -target arm64-apple-ios16.0 -isysroot "$SDK" ktcc.c -framework CoreFoundation -o out/ktcc
cp out/ktcc out/ktcc-plain
if command -v ldid >/dev/null; then
	ldid -Sktcc-entitlements.plist out/ktcc
	ldid -S../../agent/entitlements.plist out/ktcc-plain
else
	codesign -f -s - --entitlements ktcc-entitlements.plist --generate-entitlement-der out/ktcc
	codesign -f -s - --entitlements ../../agent/entitlements.plist --generate-entitlement-der out/ktcc-plain
fi
ls -l out/gl-triangle out/kjoin out/kfind out/kdmesg out/kshot out/kcsops out/ktcc out/ktcc-plain
