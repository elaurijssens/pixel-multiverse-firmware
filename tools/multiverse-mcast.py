#!/usr/bin/env python3
"""Multicast chunked frames to Multiverse W boards (E7 S7.3 test sender / reference).

Each frame is split into self-describing chunk datagrams the firmware reassembles by
offset into its back buffer (net/multicast.cpp). Chunk header (little-endian):

    magic "MVF1"(4)  frame_id(2)  flags(2)  total_len(4)  offset(4)   then payload

Payload is the raw framebuffer in PenRGB888 byte order (B, G, R, 0 per pixel), exactly
like the `data` command. Stdlib only.

Examples:
  multiverse-mcast.py --color 0,255,0                 # one green frame
  multiverse-mcast.py --cycle --fps 3                 # cycle R/G/B/... frames
"""

import argparse
import socket
import struct
import sys
import time

MAGIC = b"MVF1"
CYCLE = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255)]


def solid(w, h, rgb):
    r, g, b = rgb
    return bytes([b, g, r, 0]) * (w * h)  # PenRGB888: little-endian B,G,R,0


def send_frame(sock, dst, frame_id, frame, chunk):
    total = len(frame)
    for off in range(0, total, chunk):
        payload = frame[off:off + chunk]
        hdr = MAGIC + struct.pack("<HHII", frame_id & 0xFFFF, 0, total, off)
        sock.sendto(hdr + payload, dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--group", default="239.255.0.1")
    ap.add_argument("--port", type=int, default=54322)
    ap.add_argument("--size", default="256x64", help="WxH (must match the boards)")
    ap.add_argument("--color", default=None, help="R,G,B for a single solid frame")
    ap.add_argument("--cycle", action="store_true", help="loop R/G/B/... frames")
    ap.add_argument("--fps", type=float, default=2.0)
    ap.add_argument("--chunk", type=int, default=1400)
    ap.add_argument("--ttl", type=int, default=1)
    args = ap.parse_args()

    w, h = (int(x) for x in args.size.lower().split("x"))
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, args.ttl)
    dst = (args.group, args.port)

    try:
        if args.cycle:
            fid = 0
            while True:
                rgb = CYCLE[fid % len(CYCLE)]
                send_frame(s, dst, fid, solid(w, h, rgb), args.chunk)
                print(f"frame {fid}: {rgb}")
                fid += 1
                time.sleep(1.0 / args.fps)
        else:
            rgb = tuple(int(x) for x in (args.color or "0,255,0").split(","))
            send_frame(s, dst, 0, solid(w, h, rgb), args.chunk)
            print(f"sent 1 frame {rgb} ({w}x{h}) to {args.group}:{args.port}")
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
