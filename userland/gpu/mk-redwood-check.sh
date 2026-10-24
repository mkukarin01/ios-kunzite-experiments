#!/bin/bash
# redwood-check for the phone (arm64 iOS 16), signed ad hoc as the agent is (agent/build.sh):
#   userland/gpu/mk-redwood-check.sh   ->   userland/gpu/out/redwood-check
# Onto the phone and run, over the agent (the GPU kept: kgpu_keep=1 kgpu>=21):
#   curl -s -T userland/gpu/out/redwood-check 'http://192.168.7.2:8022/put?p=/tmp/redwood-check&m=755'
#   curl -s 'http://192.168.7.2:8022/run?a=/tmp/redwood-check'
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
clang -O2 -g -Wall -Wextra -std=c11 -target arm64-apple-ios16.0 -isysroot "$SDK" \
	redwood_check.c -framework IOKit -framework CoreFoundation -o out/redwood-check
if command -v ldid >/dev/null; then
	ldid -S../../agent/entitlements.plist out/redwood-check
else
	codesign -f -s - --entitlements ../../agent/entitlements.plist --generate-entitlement-der out/redwood-check
fi
ls -l out/redwood-check
