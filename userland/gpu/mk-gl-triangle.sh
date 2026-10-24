#!/bin/bash
# gl-triangle for the phone (arm64 iOS 16), signed ad hoc as the agent is (agent/build.sh). It loads
# libOSMesa.8.dylib (userland/mesa/mk-mesa-mac.sh) at run time, so it needs no Mesa to build:
#   userland/gpu/mk-gl-triangle.sh   ->   userland/gpu/out/gl-triangle, out/kjoin
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
for p in gl_triangle:gl-triangle kjoin:kjoin; do
	clang -O2 -g -Wall -Wextra -std=c11 -target arm64-apple-ios16.0 -isysroot "$SDK" ${p%:*}.c -o out/${p#*:}
	if command -v ldid >/dev/null; then
		ldid -S../../agent/entitlements.plist out/${p#*:}
	else
		codesign -f -s - --entitlements ../../agent/entitlements.plist --generate-entitlement-der out/${p#*:}
	fi
done
ls -l out/gl-triangle out/kjoin
