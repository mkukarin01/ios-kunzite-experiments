#!/usr/bin/env python3
"""Check an Android sparse image against the raw image it was made from.

  verify-simg.py IMAGE.simg IMAGE.img

Applies the checks libsparse makes (chunk total_sz, fill values) and compares
the expansion with the raw image byte for byte; FILL chunks must cover zero
bytes only. Exits non-zero on the first mismatch.
"""
import os
import struct
import sys


def main():
    simg, img = sys.argv[1], sys.argv[2]
    f, g = open(simg, 'rb'), open(img, 'rb')
    magic, major, _, fh, ch, blk, tblk, nchunks, _ = struct.unpack('<I4H4I', f.read(28))
    if (magic, major, fh, ch) != (0xED26FF3A, 1, 28, 12):
        sys.exit('not an Android sparse image (v1, 28/12-byte headers)')
    done, counts, zero = 0, {}, bytes(1 << 22)
    for i in range(nchunks):
        ctype, _, n, total = struct.unpack('<2H2I', f.read(12))
        counts[ctype] = counts.get(ctype, 0) + 1
        left = n * blk
        if ctype == 0xCAC1:
            if total != 12 + left:
                sys.exit(f'raw chunk {i}: total_sz {total}')
            while left:
                k = min(left, 1 << 22)
                if f.read(k) != g.read(k):
                    sys.exit(f'raw chunk {i} differs from the image')
                left -= k
        elif ctype == 0xCAC2:
            if total != 16 or struct.unpack('<I', f.read(4))[0] != 0:
                sys.exit(f'fill chunk {i}: total_sz {total} or non-zero fill')
            while left:
                k = min(left, 1 << 22)
                if g.read(k) != zero[:k]:
                    sys.exit(f'fill chunk {i} covers non-zero bytes')
                left -= k
        else:
            sys.exit(f'chunk {i}: unexpected type {ctype:#x}')
        done += n
    if done != tblk or f.read(1) or g.read(1) or os.path.getsize(img) != tblk * blk:
        sys.exit('block totals or file sizes do not match')
    print(f'VERIFIED: {nchunks} chunks (raw {counts.get(0xCAC1, 0)}, fill {counts.get(0xCAC2, 0)}), '
          f'{tblk * blk / 2**30:.1f} GiB, identical to {os.path.basename(img)}')


if __name__ == '__main__':
    main()
