#!/usr/bin/env python3
"""Receive the phone's live console (UDP datagrams to port 5140, sweet_usb.c)
and append it to a file, also echoing it to stdout. No root needed.
    tools/udp-log.py LOGFILE [PORT]
"""
import socket
import sys

path = sys.argv[1]
port = int(sys.argv[2]) if len(sys.argv) > 2 else 5140
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("0.0.0.0", port))
print(f"listening on udp/{port}, appending to {path}", file=sys.stderr, flush=True)
with open(path, "ab", buffering=0) as f:
    while True:
        data, _ = s.recvfrom(65535)
        f.write(data)
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
