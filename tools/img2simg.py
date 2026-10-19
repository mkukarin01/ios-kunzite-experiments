#!/usr/bin/env python3
"""Convert a raw disk image to Android sparse format (img2simg, macOS port).

Runs of zero blocks become "don't care" chunks (fastboot skips them, so a
mostly-empty 16G HFS+ image flashes in minutes); everything else becomes raw
chunks capped at 64 MiB. Stdlib only.

  img2simg.py IN.img OUT.simg [BLOCK_SIZE]
"""
import struct
import sys

SPARSE_MAGIC = 0xED26FF3A
CHUNK_TYPE_RAW = 0xCAC1
CHUNK_TYPE_DONT_CARE = 0xCAC3
SCAN = 1 << 20          # scan granularity
MAX_RAW = 64 << 20      # cap per raw chunk


def main():
    src, dst = sys.argv[1], sys.argv[2]
    blk = int(sys.argv[3]) if len(sys.argv) > 3 else 4096
    total_blks = 0
    chunks = []          # (type, length_bytes)

    with open(src, 'rb') as f:
        zero = bytes(SCAN)
        pending_raw = 0
        while True:
            buf = f.read(SCAN)
            if not buf:
                break
            if len(buf) < SCAN:
                buf += b'\0' * (SCAN - len(buf))
            if buf == zero:
                if pending_raw:
                    while pending_raw:
                        take = min(pending_raw, MAX_RAW)
                        chunks.append((CHUNK_TYPE_RAW, take))
                        pending_raw -= take
                if chunks and chunks[-1][0] == CHUNK_TYPE_DONT_CARE:
                    chunks[-1] = (CHUNK_TYPE_DONT_CARE, chunks[-1][1] + len(buf))
                else:
                    chunks.append((CHUNK_TYPE_DONT_CARE, len(buf)))
            else:
                if pending_raw == 0 and chunks and chunks[-1][0] == CHUNK_TYPE_RAW \
                        and chunks[-1][1] < MAX_RAW:
                    room = MAX_RAW - chunks[-1][1]
                    chunks[-1] = (CHUNK_TYPE_RAW, chunks[-1][1] + min(room, len(buf)))
                    pending_raw = len(buf) - min(room, len(buf))
                else:
                    pending_raw += len(buf)
                    while pending_raw >= MAX_RAW:
                        chunks.append((CHUNK_TYPE_RAW, MAX_RAW))
                        pending_raw -= MAX_RAW
            total_blks += len(buf) // blk

    with open(src, 'rb') as f, open(dst, 'wb') as o:
        o.write(struct.pack('<I4H4I', SPARSE_MAGIC, 1, 0, 28, 12, blk,
                            total_blks, len(chunks), 0))
        for ctype, ln in chunks:
            nblk = ln // blk
            o.write(struct.pack('<2H2I', ctype, 0, nblk, ln if ctype == CHUNK_TYPE_RAW else 0))
            if ctype == CHUNK_TYPE_RAW:
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
