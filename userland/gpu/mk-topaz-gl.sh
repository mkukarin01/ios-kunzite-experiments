#!/bin/bash
# U2 (main repo ios/gpu-glue-plan.md 4-5): what puts backboardd's CoreAnimation on the GPU, for the
# phone (arm64 iOS 16), signed ad hoc as the agent is (agent/build.sh):
#   [MESA_BUILD=DIR] [BBD_REAL=PATH] userland/gpu/mk-topaz-gl.sh   ->   userland/gpu/out/
#     eagl_gen.h              EAGLContext's function table, eagl/mkeagl.py over eagl/eagl_slots.txt
#     libtopaz-gl.dylib       eagl.c + gl_osmesa.c over libOSMesa.8.dylib              -> /topaz/lib/
#     libtopaz-inject.dylib   topaz_inject.c: loads libtopaz-gl into backboardd         -> /topaz/lib/
#     backboardd              bbd_wrap.c: execs BBD_REAL with the loader in DYLD_INSERT_LIBRARIES
#     eagl-check              eagl_check.m: the table through iOS's own EAGLContext and OpenGL ES, in a
#                             plain program, before U4 (`soft`: softpipe, no GPU needed)
# DIR is mk-mesa-mac.sh's (default userland/build-mesa): its out/ has libOSMesa.8.dylib (install name
# /topaz/lib/libOSMesa.8.dylib, so it goes to /topaz/lib/ too), gl.xml and exported-gl.txt.
# BBD_REAL: /usr/libexec/backboardd (default: the wrapper goes to /topaz/bin/backboardd and the
# launchd.plist cache's com.apple.backboardd points there, glue plan 3.6 way 2), or
# /usr/libexec/real/backboardd (topaz's way 1: the stock binary moved there, the wrapper in its place).
set -euo pipefail
cd "$(dirname "$0")"
M=${MESA_BUILD:-$(cd .. && pwd)/build-mesa}/out
REAL=${BBD_REAL:-/usr/libexec/backboardd}
for f in libOSMesa.8.dylib gl.xml exported-gl.txt; do
	[ -f "$M/$f" ] || { echo "error: $M/$f missing (userland/mesa/mk-mesa-mac.sh, or set MESA_BUILD)" >&2; exit 1; }
done
mkdir -p out
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CC=(clang -O2 -g -Wall -target arm64-apple-ios16.0 -isysroot "$SDK")
python3 -I eagl/mkeagl.py eagl/eagl_slots.txt "$M/gl.xml" "$M/exported-gl.txt" eagl.c > out/eagl_gen.h
"${CC[@]}" -dynamiclib -install_name /topaz/lib/libtopaz-gl.dylib -Iout eagl.c gl_osmesa.c "$M/libOSMesa.8.dylib" \
	-framework CoreFoundation -framework IOKit -framework IOSurface -lobjc -o out/libtopaz-gl.dylib
"${CC[@]}" -dynamiclib -install_name /topaz/lib/libtopaz-inject.dylib topaz_inject.c \
	-framework CoreFoundation -framework IOKit -lobjc -o out/libtopaz-inject.dylib
"${CC[@]}" -DREAL="\"$REAL\"" bbd_wrap.c -o out/backboardd
"${CC[@]}" eagl_check.m -framework OpenGLES -framework Foundation -lobjc -o out/eagl-check
# (eagl-check as the other test programs are, with the agent's entitlements: it is run over the agent)
for f in out/libtopaz-gl.dylib out/libtopaz-inject.dylib out/backboardd out/eagl-check; do
	ENT=
	[ "$f" = out/eagl-check ] && ENT=../../agent/entitlements.plist
	if command -v ldid >/dev/null; then
		ldid -S$ENT "$f"
	elif [ -n "$ENT" ]; then
		codesign -f -s - --entitlements "$ENT" --generate-entitlement-der "$f"
	else
		codesign -f -s - "$f"
	fi
done
ls -l out/eagl_gen.h out/libtopaz-gl.dylib out/libtopaz-inject.dylib out/backboardd out/eagl-check
