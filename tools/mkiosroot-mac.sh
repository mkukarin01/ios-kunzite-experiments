#!/bin/bash
# macOS port of mkiosroot.sh: build the SWEET iOS root image (one case-sensitive
# journaled HFS+ volume holding the iOS 16.7.16 OS root + its cryptexes) for the
# kunzite userdata partition (rd=ufsroot0), without a Linux box.
#
#   mkiosroot-mac.sh iPhone10,3_..._Restore.ipsw OUT.img [SIZE]      (SIZE default 16G)
#
# Tool mapping vs the Linux original:
#   apfs-fuse            -> hdiutil attach (macOS mounts APFS/UDIF natively)
#   truncate + mkfs.hfs  -> hdiutil create -type SPARSE -layout NONE -fs HFS+J
#   rsync -aH (root)     -> rsync -a --no-D (user; no device nodes - devfs makes
#                           them) + an ownership manifest replayed as root inside
#                           a docker container (tools/apply-owners.py)
#   fsck.hfsplus         -> fsck_hfs (both are Apple's)
#   img2simg             -> tools/img2simg.py
# Ownership matters (mobile=501, _logd=272, appuidd=33...): captured from the
# read-only IPSW mounts, replayed after the copy together with
# tools/sweet-root-tweaks.py in the container. Bash 3.2 compatible.
set -euo pipefail

[[ $# -ge 2 ]] || { sed -n '2,17p' "$0"; exit 2; }
ipsw=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
out=$2; size=${3:-16G}
[[ ! -e $out && ! -e $out.sparseimage ]] || { echo "refusing to overwrite $out" >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
# Under the repo (temp/): inside /Users, so the docker bind mounts always work.
work=$(mktemp -d /Users/oxa1/Work/rtk-tablet/temp/ios-m8/rootbuild.XXXXXX)
vol="$work/root"
mkdir -p "$vol"

mounts=()
cleanup() {
	local m
	for m in "${mounts[@]}"; do hdiutil detach "$m" -force >/dev/null 2>&1 || true; done
}
trap cleanup EXIT

# --- which dmg is which (BuildManifest, D22AP Erase identity) ---
dmgs=()
while IFS= read -r l; do dmgs+=("$l"); done < <(python3 - "$ipsw" <<'PYEOF'
import plistlib, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
pl = plistlib.loads(z.read('BuildManifest.plist'))
for ident in pl['BuildIdentities']:
    info = ident['Info']
    if info.get('DeviceClass', '').lower() == 'd22ap' and info.get('RestoreBehavior') == 'Erase':
        for key in ('OS', 'Cryptex1,SystemOS', 'Cryptex1,AppOS'):
            print(ident['Manifest'][key]['Info']['Path'])
        break
else:
    sys.exit('no d22ap Erase identity in BuildManifest')
PYEOF
)
echo "OS=${dmgs[0]} SystemOS=${dmgs[1]} AppOS=${dmgs[2]}"

names=(os sysos appos)
for i in 0 1 2; do
	(cd "$work" && unzip -qo "$ipsw" "${dmgs[$i]}")
	ln -sf "$work/${dmgs[$i]}" "$work/${names[$i]}.dmg"
done
echo 'dmgs extracted'

# apfs-fuse mounts show the volume contents under root/; hdiutil mounts the
# volume directly - detect once per image.
srcbase() {
	if [[ -d "$1/root" && -d "$1/root/System" ]]; then echo "$1/root"; else echo "$1"; fi
}
attach_ro() {
	mkdir -p "$3"
	hdiutil attach "$work/$2.dmg" -readonly -nobrowse -mountpoint "$3" >/dev/null
	mounts+=("$3")
}
attach_ro "${dmgs[0]}" os "$work/os"
attach_ro "${dmgs[1]}" sysos "$work/sysos"
attach_ro "${dmgs[2]}" appos "$work/appos"
os_src=$(srcbase "$work/os"); sysos_src=$(srcbase "$work/sysos"); appos_src=$(srcbase "$work/appos")
echo "sources: $os_src | $sysos_src | $appos_src"

# --- ownership manifest from the read-only mounts ---
python3 - "$os_src" '' "$sysos_src" 'private/preboot/Cryptexes/OS' "$appos_src" 'private/preboot/Cryptexes/App' "$work/owners.json" <<'PYEOF'
import json, os, sys
manifest = {}
pairs = [(sys.argv[1], sys.argv[2]), (sys.argv[3], sys.argv[4]), (sys.argv[5], sys.argv[6])]
for base, name in pairs:
    for dirpath, dirs, files in os.walk(base):
        rel = os.path.relpath(dirpath, base)
        for entry in ('.',) + tuple(dirs) + tuple(files):
            p = os.path.join(dirpath, entry)
            try:
                st = os.lstat(p)
            except OSError:
                continue
            r = name if entry == '.' else os.path.normpath(os.path.join(name, rel, entry))
            manifest[r.lstrip('/')] = [st.st_uid, st.st_gid, st.st_mode & 0o7777]
json.dump(manifest, open(sys.argv[7], 'w'))
print('manifest entries:', len(manifest))
PYEOF

# --- target: bare journaled case-sensitive HFS+, no partition map ---
hdiutil create "$work/out.sparseimage" -type SPARSE -layout NONE \
	-fs 'Case-sensitive Journaled HFS+' -volname iOSRoot -size "$size" >/dev/null
hdiutil attach "$work/out.sparseimage" -nobrowse -mountpoint "$vol" >/dev/null
mounts=("$vol" "${mounts[@]}")
echo "target mounted at $vol"

copy() {
	echo "copying $1 -> ${2#$vol}"
	mkdir -p "$2"
	# .file is a mode-000 volume marker (unreadable as a plain user); rsync
	# exit <= 24 means partial transfer - tolerated, the copy is complete
	# enough (only .file is excluded).
	set +e
	rsync -a --no-D --exclude='/.file' "$1/" "$2/"
	rc=$?
	set -e
	[[ $rc -le 24 ]] || { echo "rsync rc=$rc" >&2; exit $rc; }
	[[ $rc -eq 0 ]] || echo "rsync rc=$rc (partial, continuing)" >&2
}
copy "$os_src" "$vol"
copy "$sysos_src" "$vol/private/preboot/Cryptexes/OS"
copy "$appos_src" "$vol/private/preboot/Cryptexes/App"
: > "$vol/.file"

# Shared cache where dyld looks at ignition level 1: hard links, no extra space.
dsc=private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld
mkdir -p "$vol/System/Library/Caches/com.apple.dyld"
for f in "$vol/$dsc"/*; do
	[[ -e "$vol/System/Library/Caches/com.apple.dyld/${f##*/}" ]] || ln "$f" "$vol/System/Library/Caches/com.apple.dyld/${f##*/}"
done

# Cryptex files without a root counterpart: hard-link into the root (macOS
# HFS+ can hard-link symlinks too, no Linux workaround needed).
for c in OS App; do
	(cd "$vol/private/preboot/Cryptexes/$c" && find . ! -type d ! -path './System/Library/Caches/com.apple.dyld/*') |
	while IFS= read -r f; do
		f=${f#./}
		if [[ ! -e "$vol/$f" && ! -L "$vol/$f" ]]; then
			mkdir -p "$vol/$(dirname "$f")"
			ln "$vol/private/preboot/Cryptexes/$c/$f" "$vol/$f" 2>/dev/null || \
				cp -p "$vol/private/preboot/Cryptexes/$c/$f" "$vol/$f"
		fi
	done
done

# logd stores (_logd=272 comes from the manifest replay) and the keybag.
mkdir -p "$vol/private/var/db/diagnostics/timesync" "$vol/private/var/db/uuidtext" "$vol/private/var/keybags"
for d in diagnostics uuidtext diagnostics/timesync; do : > "$vol/private/var/db/$d/.migrated"; done
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' \
	> "$vol/private/var/keybags/systembag.kb"
chmod 0751 "$vol/private/var/db/diagnostics"
chmod 0755 "$vol/private/var/db/uuidtext" "$vol/private/var/db/diagnostics/timesync"
chmod 0600 "$vol/private/var/keybags/systembag.kb"
chmod 0700 "$vol/private/var/keybags"

df -h "$vol"

# --- ownership replay + bring-up tweaks, as root inside docker ---
if command -v docker >/dev/null && docker info >/dev/null 2>&1; then
	docker run --rm -v "$vol:/r" -v "$here:/tools" -v "$work:/w" python:3-slim \
		sh -c 'python3 /tools/apply-owners.py /w/owners.json /r && python3 /tools/sweet-root-tweaks.py /r' || \
		echo "WARNING: container pass failed - apply manually before flashing" >&2
else
	echo 'WARNING: docker unavailable - ownership and tweaks NOT applied.' >&2
	echo "later, as root: python3 $here/apply-owners.py $work/owners.json $vol && python3 $here/sweet-root-tweaks.py $vol" >&2
fi

hdiutil detach "$vol" -force >/dev/null
mounts=("${mounts[@]:1}")
# hdiutil convert appends .dmg to -o names: ask for it, then strip it.
hdiutil convert "$work/out.sparseimage" -format UDRW -o "$out.dmg" >/dev/null
mv "$out.dmg" "$out"
rm -f "$work/out.sparseimage"
fsck_hfs -y "$out" >/dev/null || true
fsck_hfs -n "$out" 2>&1 | tail -1
python3 "$here/img2simg.py" "$out" "$out.simg"
ls -l "$out" "$out.simg"
shasum -a 256 "$out" | tee "$out.sha256"
