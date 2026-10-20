#!/usr/bin/env python3
"""Apply an ownership/mode manifest (tools/capture-owners.py) to an iOS root.

  sudo apply-owners.py MANIFEST.json ROOT

Must run as real root on a volume mounted with owners enabled (macOS:
`hdiutil attach -owners on`). Docker Desktop bind mounts silently drop chown
on macOS, so do not run it in a container. Symlinks are changed with lchown
and keep their mode; everything else gets chown then chmod (chmod after chown
restores setuid/setgid bits a chown may clear).
"""
import json
import os
import stat
import sys


def main():
    manifest, root = json.load(open(sys.argv[1])), sys.argv[2]
    n = missing = errors = 0
    for rel, (uid, gid, mode) in manifest.items():
        path = os.path.join(root, rel)
        try:
            st = os.lstat(path)
            os.lchown(path, uid, gid)
            if not stat.S_ISLNK(st.st_mode):
                os.chmod(path, mode)
            n += 1
        except FileNotFoundError:
            missing += 1
        except OSError as e:
            errors += 1
            print(f'warn: {rel}: {e}', file=sys.stderr)
    print(f'owners applied: {n}, missing: {missing}, errors: {errors}')


if __name__ == '__main__':
    main()
