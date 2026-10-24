#!/bin/bash
# gl-triangle for the phone (arm64 iOS 16), signed ad hoc as the agent is (agent/build.sh). It loads
# libOSMesa.8.dylib (userland/mesa/mk-mesa-mac.sh) at run time, so it needs no Mesa to build:
#   userland/gpu/mk-gl-triangle.sh   ->   userland/gpu/out/gl-triangle
# Onto the phone and run, over the agent (the GPU kept: kgpu_keep=1 kgpu>=21):
#   curl -s -T $MESA_BUILD/out/libOSMesa.8.dylib 'http://192.168.7.2:8022/put?p=/tmp/libOSMesa.8.dylib&m=644'
#   curl -s -T userland/gpu/out/gl-triangle 'http://192.168.7.2:8022/put?p=/tmp/gl-triangle&m=755'
#   curl -s 'http://192.168.7.2:8022/run?a=/tmp/gl-triangle&t=60'
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
clang -O2 -g -Wall -Wextra -std=c11 -target arm64-apple-ios16.0 -isysroot "$SDK" gl_triangle.c -o out/gl-triangle
if command -v ldid >/dev/null; then
	ldid -S../../agent/entitlements.plist out/gl-triangle
else
	codesign -f -s - --entitlements ../../agent/entitlements.plist --generate-entitlement-der out/gl-triangle
fi
ls -l out/gl-triangle
