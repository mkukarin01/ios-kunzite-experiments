#!/bin/bash
# Install kunzite-agent into an iOS root image (in place; run as root).
#   sudo tools/root-add-agent-mac.sh [IMAGE] [AGENT_BINARY]
#
# IMAGE defaults to temp/ios-m8/ios16-root-20H392.img, the binary to
# agent/out/kunzite-agent.ios. What it does:
#   - copies the binary to /usr/libexec/kunzite-agent (root:wheel 0755)
#   - adds the launchd job to the launchd.plist cache (the LaunchDaemons
#     dictionary launchd reads with launchd_unsecure_cache=1) and writes
#     /System/Library/LaunchDaemons/com.kunzite.agent.plist with the same job
#   - stdout/stderr go to /dev/console = the live USB log
# Idempotent. Afterwards (as yourself):
#   tools/img2simg.py IMAGE IMAGE.simg && fastboot flash userdata IMAGE.simg
set -euo pipefail
[[ $EUID == 0 ]] || { sed -n '3,8p' "$0"; echo "run with sudo" >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
img=${1:-$here/../../../temp/ios-m8/ios16-root-20H392.img}
bin=${2:-$here/../agent/out/kunzite-agent.ios}
[[ -f "$img" ]] || { echo "no such image: $img" >&2; exit 2; }
[[ -f "$bin" ]] || { echo "no such binary: $bin (run agent/build.sh)" >&2; exit 2; }
[[ $(stat -f %Sp "$bin") == *x* ]] || { echo "binary not executable: $bin" >&2; exit 2; }

mnt=$(mktemp -d /tmp/iosroot.XXXXXX)
hdiutil attach "$img" -owners on -nobrowse -mountpoint "$mnt" >/dev/null
trap 'hdiutil detach "$mnt" >/dev/null 2>&1 || hdiutil detach "$mnt" -force >/dev/null 2>&1; rmdir "$mnt" 2>/dev/null || true' EXIT
[[ -d "$mnt/System/Library/xpc" ]] || { echo "$img does not look like an iOS root" >&2; exit 1; }

install -m 0755 -o 0 -g 0 "$bin" "$mnt/usr/libexec/kunzite-agent"
echo "installed /usr/libexec/kunzite-agent ($(stat -Lf %z "$mnt/usr/libexec/kunzite-agent") bytes)"

python3 - "$mnt" <<'PYEOF'
import os, plistlib, sys
root = sys.argv[1]

job = {
    'Label': 'com.kunzite.agent',
    'Program': '/usr/libexec/kunzite-agent',
    'RunAtLoad': True,
    'KeepAlive': True,
    'LimitLoadToSessionType': 'System',
    'ThrottleInterval': 2,
    'StandardOutPath': '/dev/console',
    'StandardErrorPath': '/dev/console',
}
plist_path = os.path.join(root, 'System/Library/LaunchDaemons/com.kunzite.agent.plist')
st = os.stat(plist_path) if os.path.exists(plist_path) else None
with open(plist_path, 'wb') as f:
    plistlib.dump(job, f, fmt=plistlib.FMT_XML)
if st:
    os.chown(plist_path, st.st_uid, st.st_gid)
    os.chmod(plist_path, st.st_mode & 0o7777)
else:
    os.chown(plist_path, 0, 0)
    os.chmod(plist_path, 0o644)

cache = os.path.join(root, 'System/Library/xpc/launchd.plist')
st = os.stat(cache)
with open(cache, 'rb') as f:
    p = plistlib.load(f)
key = '/System/Library/LaunchDaemons/com.kunzite.agent.plist'
p['LaunchDaemons'][key] = job
with open(cache, 'wb') as f:
    plistlib.dump(p, f, fmt=plistlib.FMT_BINARY)
os.chown(cache, st.st_uid, st.st_gid)
os.chmod(cache, st.st_mode & 0o7777)
print('launchd.plist: job', job['Label'], 'installed (RunAtLoad, KeepAlive, console logs)')
PYEOF

sync
echo "done. Flash next (as yourself):"
echo "  tools/img2simg.sh step: python3 tools/img2simg.py $img ${img%.img}.simg"
echo "  fastboot flash userdata ${img%.img}.simg"
