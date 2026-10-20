#!/bin/bash
# Give an iOS root image (tools/mkiosroot-mac.sh) its real ownership, as root.
#
#   sudo tools/fix-owners-mac.sh ROOT.img OWNERS.json
#
# OWNERS.json comes from tools/capture-owners.py over the IPSW dmgs attached
# with `hdiutil attach -owners on`. Order: everything root:wheel first (the
# default for paths the builder created itself), then the manifest (lchown +
# chmod, which restores setuid bits), then the paths the builder made with
# known owners, then sweet-root-tweaks.py again (it re-saves the mobile
# preferences as 501:501). Run on macOS directly: Docker Desktop bind mounts
# silently drop chown.
set -euo pipefail
[[ $EUID == 0 ]] || { echo "run with sudo" >&2; exit 1; }
[[ $# == 2 ]] || { sed -n '2,12p' "$0"; exit 2; }
img=$1 manifest=$2
here=$(cd "$(dirname "$0")" && pwd)
mnt=$(mktemp -d /tmp/iosroot.XXXXXX)
hdiutil attach "$img" -owners on -nobrowse -mountpoint "$mnt" >/dev/null
trap 'hdiutil detach "$mnt" >/dev/null 2>&1 || hdiutil detach "$mnt" -force >/dev/null 2>&1; rmdir "$mnt" 2>/dev/null || true' EXIT
[[ -d "$mnt/System/Library/xpc" ]] || { echo "$img does not look like an iOS root" >&2; exit 1; }

echo "root:wheel everywhere..."
chown -R -h 0:0 "$mnt"
echo "manifest..."
python3 "$here/apply-owners.py" "$manifest" "$mnt"

# Made by the builder (mkiosroot-mac.sh), not in the IPSW: logd's stores
# (_logd 272, as the Linux mkiosroot.sh installs them) and the containers
# (owners as sweet-root-tweaks.py creates them).
db="$mnt/private/var/db"
for d in diagnostics uuidtext diagnostics/timesync; do
	[[ -e "$db/$d" ]] && chown 272:272 "$db/$d"
	[[ -e "$db/$d/.migrated" ]] && chown 272:272 "$db/$d/.migrated"
done
c="$mnt/private/var/containers"
[[ -d "$c/Bundle" ]] && chown 33:33 "$c/Bundle"

echo "bring-up tweaks (idempotent; re-saves prefs as mobile)..."
python3 "$here/sweet-root-tweaks.py" "$mnt" >/dev/null

echo "check:"
stat -f '  %u:%g %Sp  %N' "$mnt/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64" \
	"$mnt/sbin/launchd" "$mnt/private/var/mobile" "$mnt/private/var/db/diagnostics" \
	"$mnt/private/var/keybags/systembag.kb" "$mnt/private/var/mobile/Library/Preferences/com.apple.springboard.plist"
sync
echo "done; now (as yourself): tools/img2simg.py $img ${img%.img}.simg"
