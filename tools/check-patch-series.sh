#!/bin/bash
# Replay CI's patch step locally and list the patches it would SKIP.
# blacktop's darwin-xnu-build (build.sh, macOS 13.5) runs xnu/patches/*.sh,
# then `git apply`s every *.patch (ours + its own) in glob order and silently
# skips any that does not apply - a broken merge still builds. Run this
# after merging upstream topaz or regenerating a patch.
#   tools/check-patch-series.sh XNU_PRISTINE DXB_CHECKOUT OUT_TREE [PATCH_DIR]
# XNU_PRISTINE: git clone of apple-oss-distributions/xnu at xnu-8796.141.3
# DXB_CHECKOUT: blacktop/darwin-xnu-build at the workflow's DXB_SHA
# Expected skips (other macOS versions): TrustCache, iobuffermemd_monterey,
# skywalk_sonoma, syntax_checker_sonoma. Anything else is a real failure.
set -euo pipefail
XNU=$(cd "$1" && pwd)
DXB=$(cd "$2" && pwd)
OUT=$(mkdir -p "$3" && cd "$3" && pwd)
SRC=$(cd "${4:-$(dirname "$0")/../xnu/patches}" && pwd)
rm -rf "$OUT" "$OUT.patches"
mkdir -p "$OUT" "$OUT.patches"
git -C "$XNU" archive HEAD | tar -x -C "$OUT"
git -C "$OUT" init -q
cp "$SRC"/*.patch "$DXB"/patches/*.patch "$OUT.patches"/
compgen -G "$SRC/*.sh" >/dev/null && cp "$SRC"/*.sh "$OUT.patches"/
cd "$OUT"
for s in "$OUT.patches"/*.sh; do
	[ -e "$s" ] || continue
	MACHINE_CONFIG=SWEET MACOS_VERSION=13.5 KDKROOT=/nonexistent bash "$s" >/dev/null 2>&1 ||
		echo "SCRIPT FAILED $(basename "$s")"
done
applied=0
for p in "$OUT.patches"/*.patch; do
	if msg=$(git apply --check "$p" 2>&1); then
		git apply "$p" 2>/dev/null
		applied=$((applied + 1))
	else
		echo "SKIPPED $(basename "$p"): $(echo "$msg" | grep -m1 error)"
	fi
done
echo "applied $applied"
