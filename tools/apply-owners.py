#!/usr/bin/env python3
"""Apply an ownership/mode manifest captured from the source APFS mounts.

macOS cannot chown to arbitrary uids without root, so mkiosroot-mac.sh records
(uid, gid, mode) for every path while the IPSW dmgs are mounted (readable as
a plain user) and this script replays them inside a root docker container.

  apply-owners.py MANIFEST.json ROOT
"""
import json
import os
import sys


def main():
    manifest, root = json.load(open(sys.argv[1])), sys.argv[2]
    n = missing = 0
    for rel, (uid, gid, mode) in manifest.items():
        path = os.path.join(root, rel)
        try:
            os.chown(path, uid, gid)
            os.chmod(path, mode)
            n += 1
        except FileNotFoundError:
            missing += 1
        except OSError as e:
            print(f'warn: {rel}: {e}', file=sys.stderr)
    print(f'owners applied: {n}, missing: {missing}')
    # Missing/warned entries (hardlink duplicates, excluded markers, suid
    # files the sharing layer refuses) are tolerable: never block the chain.
    sys.exit(0)


if __name__ == '__main__':
    main()
