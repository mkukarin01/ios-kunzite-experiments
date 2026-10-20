#!/usr/bin/env python3
"""Receive the phone's live console (UDP datagrams to port 5140, sweet_usb.c)
and append it to a file, also echoing it to stdout. No root needed.

    tools/udp-log.py LOGFILE [--tail N | --no-replay] [--port P]

At start it asks the phone (192.168.7.2, patch 0910) to resend its console
ring - the whole ring by default, the last N bytes with --tail - and repeats
the request every 2 s until the first datagram arrives, so the log starts at
the boot whenever the listener is started.
"""
import argparse
import socket
import sys
import time

PHONE = ("192.168.7.2", 5140)

ap = argparse.ArgumentParser()
ap.add_argument("logfile")
ap.add_argument("--port", type=int, default=5140)
ap.add_argument("--tail", type=int, help="ask for the last N bytes instead of the whole ring")
ap.add_argument("--no-replay", action="store_true", help="only new output")
args = ap.parse_args()

request = None if args.no_replay else (f"tail {args.tail}" if args.tail else "replay").encode()
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
s.bind(("0.0.0.0", args.port))
s.settimeout(2.0)
print(f"listening on udp/{args.port}, appending to {args.logfile}", file=sys.stderr, flush=True)
got_any = False
last_req = 0.0
with open(args.logfile, "ab", buffering=0) as f:
    while True:
        if request and not got_any and time.monotonic() - last_req >= 2.0:
            last_req = time.monotonic()
            try:
                s.sendto(request, PHONE)
            except OSError:
                pass        # no address/route yet: the phone is not up
        try:
            data, _ = s.recvfrom(65535)
        except socket.timeout:
            continue
        got_any = True
        f.write(data)
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
