#!/usr/bin/env python3
"""Convert a raw disk image to Android sparse format (img2simg, macOS port).

Runs of zero blocks become FILL chunks with value 0 (the device writes zeros
without transferring them); everything else becomes raw chunks capped at 64
MiB. Never DONT_CARE: that leaves the partition's old bytes in place, and a
filesystem image's zero ranges (inside files too) must read back as zeros.
Stdlib only.

  img2simg.py IN.img OUT.simg [BLOCK_SIZE]
"""
import struct
import sys

SPARSE_MAGIC = 0xED26FF3A
CHUNK_TYPE_RAW = 0xCAC1
CHUNK_TYPE_FILL = 0xCAC2
SCAN = 1 << 20          # scan granularity
MAX_RAW = 64 << 20      # cap per raw chunk


def main():
    src, dst = sys.argv[1], sys.argv[2]
    blk = int(sys.argv[3]) if len(sys.argv) > 3 else 4096
    # Pass 1: runs of zero / non-zero SCAN-sized pieces; raw runs split at
    # MAX_RAW. (The first version dropped a trailing raw run and wrote
    # chunk sizes without the 12-byte header - both broke fastboot.)
    total_blks = 0
    chunks = []          # (type, length_bytes)
    with open(src, 'rb') as f:
        while True:
            buf = f.read(SCAN)
            if not buf:
                break
            if len(buf) % blk:
                buf += b'\0' * (blk - len(buf) % blk)
            ctype = CHUNK_TYPE_FILL if buf.count(0) == len(buf) else CHUNK_TYPE_RAW
            if chunks and chunks[-1][0] == ctype and \
                    (ctype == CHUNK_TYPE_FILL or chunks[-1][1] + len(buf) <= MAX_RAW):
                chunks[-1] = (ctype, chunks[-1][1] + len(buf))
            else:
                chunks.append((ctype, len(buf)))
            total_blks += len(buf) // blk

    with open(src, 'rb') as f, open(dst, 'wb') as o:
        o.write(struct.pack('<I4H4I', SPARSE_MAGIC, 1, 0, 28, 12, blk,
                            total_blks, len(chunks), 0))
        for ctype, ln in chunks:
            nblk = ln // blk
            # total_sz counts the 12-byte chunk header too (libsparse checks it)
            if ctype == CHUNK_TYPE_FILL:
                o.write(struct.pack('<2H2I', ctype, 0, nblk, 12 + 4))
                o.write(struct.pack('<I', 0))   # fill value
                f.seek(ln, 1)       # keep the source in step with the chunks
            if ctype == CHUNK_TYPE_RAW:
                o.write(struct.pack('<2H2I', ctype, 0, nblk, 12 + ln))
                left = ln
                while left:
                    buf = f.read(min(1 << 20, left))
                    if not buf:
                        buf = b'\0' * min(1 << 20, left)
                    o.write(buf)
                    left -= len(buf)
    print(f'{dst}: {len(chunks)} chunks, {total_blks} blocks of {blk}', file=sys.stderr)


if __name__ == '__main__':
    main()
