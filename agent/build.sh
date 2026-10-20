#!/bin/bash
# Build kunzite-agent: out/kunzite-agent.ios (arm64 iOS 16, ldid-signed) and
# out/kunzite-agent.mac (same source, for local tests on this machine).
#   agent/build.sh          both
#   agent/build.sh mac      the Mac binary only
#   agent/build.sh ios      the iOS binary only
set -euo pipefail
cd "$(dirname "$0")"
what=${1:-all}
mkdir -p out
COMMON="-O2 -g -Wall -Wextra -std=c11"

if [ "$what" = all ] || [ "$what" = ios ]; then
	SDK=$(xcrun --sdk iphoneos --show-sdk-path)
	clang $COMMON -target arm64-apple-ios16.0 -isysroot "$SDK" \
		kunzite-agent.c -o out/kunzite-agent.ios
	if command -v ldid >/dev/null; then
		ldid -Sentitlements.plist out/kunzite-agent.ios
	else
		codesign -f -s - --entitlements entitlements.plist \
			--generate-entitlement-der out/kunzite-agent.ios
	fi
	codesign -dv out/kunzite-agent.ios 2>&1 | head -3 || true
	ls -l out/kunzite-agent.ios
fi
if [ "$what" = all ] || [ "$what" = mac ]; then
	clang $COMMON kunzite-agent.c -o out/kunzite-agent.mac
	ls -l out/kunzite-agent.mac
fi
