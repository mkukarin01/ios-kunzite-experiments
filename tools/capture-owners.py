#!/usr/bin/env python3
"""Record (uid, gid, mode) of every path in mounted IPSW volumes.

  capture-owners.py OUT.json SRC1 PREFIX1 [SRC2 PREFIX2 ...]

SRCn is a mounted volume (or its root/ directory), PREFIXn where its tree
lands inside the iOS root ('' for the OS volume, private/preboot/Cryptexes/OS
for the SystemOS cryptex, ...). Mount the dmgs with `hdiutil attach -owners on`:
without it macOS reports the current user as the owner of every file (that is
how the first kunzite image ended up owned by 501 throughout). Refuses to
write a manifest in which every entry has the same owner.
"""
import collections
import json
import os
import sys


def main():
    if len(sys.argv) < 4 or len(sys.argv) % 2:
        sys.exit(__doc__)
    out, pairs = sys.argv[1], list(zip(sys.argv[2::2], sys.argv[3::2]))
    manifest = {}
    for base, name in pairs:
        for dirpath, dirs, files in os.walk(base):
            rel = os.path.relpath(dirpath, base)
            for entry in ('.',) + tuple(dirs) + tuple(files):
                try:
                    st = os.lstat(os.path.join(dirpath, entry))
                except OSError:
                    continue
                r = name if entry == '.' else os.path.normpath(os.path.join(name, rel, entry))
                manifest[r.lstrip('/')] = [st.st_uid, st.st_gid, st.st_mode & 0o7777]
    uids = collections.Counter(v[0] for v in manifest.values())
    print(f'manifest entries: {len(manifest)}, uids: {uids.most_common(6)}')
    if len(uids) < 2:
        sys.exit('refusing: one owner for everything - were the dmgs attached with -owners on?')
    json.dump(manifest, open(out, 'w'))


if __name__ == '__main__':
    main()
