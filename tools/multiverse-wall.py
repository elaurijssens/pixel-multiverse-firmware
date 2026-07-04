#!/usr/bin/env python3
"""Split an image / animated GIF / WebP / video across a wall of Multiverse screens.

Each screen shows a rectangular *region* of a larger composite; this tool crops that
region per frame, packs it into the firmware framebuffer format, and streams it to the
board over **WiFi** (UDP MVF1 chunk frames → the multicast receiver, E7 S7.3) or **USB**
(the `data` command). Animated media loops at its own frame rate (or --fps).

Video (mp4/mov/webm/mkv/…) is decoded and scaled by **ffmpeg** (must be on PATH) — real
video avoids GIF's 256-colour banding. Stills (and slow frames) are re-sent periodically
so a dropped packet can't leave them blank/partial.

Describe the wall in a JSON config:

    {
      "size": [256, 128],          # composite pixel size (all regions live in here)
      "fps": 15,                   # optional; overrides the media's own timing
      "fit": "stretch",            # stretch | contain | cover (how media maps to `size`)
      "boards": [
        {"target": "udp:192.168.15.147", "at": [0, 0],  "size": [256, 64]},
        {"target": "udp:192.168.13.174", "at": [0, 64], "size": [256, 64]}
      ]
    }

  multiverse-wall.py my.gif --config wall.json --forever
  multiverse-wall.py my.gif --size 256x128 \
      --board udp:192.168.15.147@0,0+256x64 --board usb:/dev/cu.usbmodem1101@0,64+256x64

target = udp:IP[:port]  (default port 54322)  |  usb:/dev/serialdevice
region = @X,Y+WxH ; a board's WxH must match the size that board is configured for.
Needs Pillow.
"""

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import time
import zlib

try:
    from PIL import Image, ImageSequence
except ImportError:
    sys.exit("error: needs Pillow  (pip install Pillow)")

MVF1 = b"MVF1"
FLAG_FLIP = 0x0001          # MVF1 flags bit — present the back buffer (no payload)
FLAG_ZLIB = 0x0002          # MVF1 flags bit — payload is a zlib-compressed frame
DEFAULT_UDP_PORT = 54322    # multicast frame group / unicast frame port
DEFAULT_CMD_PORT = 54321    # UDP command socket (multiverse: prefix), for hold/live
CHUNK = 1456                # UDP payload minus MVF1 header → datagram fits one 1500B MTU
PREFIX = b"multiverse:"


# ---- framebuffer packing --------------------------------------------------------------

def pack_penrgb888(rgb_image):
    """RGB PIL image → firmware bytes (little-endian B, G, R, 0 per pixel)."""
    px = rgb_image.tobytes()               # R G B  R G B ...
    out = bytearray((len(px) // 3) * 4)
    out[0::4] = px[2::3]                    # B
    out[1::4] = px[1::3]                    # G
    out[2::4] = px[0::3]                    # R
    return bytes(out)                       # byte 3 stays 0 (unused pen byte)


# ---- sinks (one per board) ------------------------------------------------------------

def _open_raw(device):
    import termios
    fd = os.open(device, os.O_RDWR | os.O_NOCTTY)
    try:
        a = termios.tcgetattr(fd)
        a[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP |
                  termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
        a[1] &= ~termios.OPOST
        a[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
        termios.tcsetattr(fd, termios.TCSANOW, a)
    except termios.error:
        pass
    return fd


class UdpSink:
    on_group = True                          # reachable by a multicast flip

    def __init__(self, ip, port, cmd_port=DEFAULT_CMD_PORT, compress=True, repeat=1):
        self.ip = ip
        self.addr = (ip, port)
        self.cmd_port = cmd_port
        self.compress = compress
        self.repeat = max(1, repeat)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
        self.fid = 0

    def encode(self, frame):
        # zlib-compress ahead of the timed send→flip window (compression is several ms —
        # keeping it off the critical path is what stops the flip racing an un-sent slice).
        if self.compress:
            z = zlib.compress(frame, 6)
            if len(z) < len(frame):
                return (FLAG_ZLIB, z)
        return (0, frame)

    def transmit(self, enc):
        flags, payload = enc
        total = len(payload)
        # Send the whole frame `repeat` times under ONE frame id — the board dedups by
        # offset, so a chunk lost in one pass is filled by the next. Copies are sent
        # pass-after-pass (not chunk-adjacent) so a burst can't take out both copies.
        for _ in range(self.repeat):
            for off in range(0, total, CHUNK):
                hdr = MVF1 + struct.pack("<HHII", self.fid & 0xFFFF, flags, total, off)
                self.sock.sendto(hdr + payload[off:off + CHUNK], self.addr)
        self.fid += 1

    def send(self, frame):                   # convenience: encode + transmit in one call
        self.transmit(self.encode(frame))

    def send_command(self, cmd):             # multiverse: command to the UDP command socket
        self.sock.sendto(PREFIX + cmd.encode(), (self.ip, self.cmd_port))

    def __str__(self):
        return f"udp:{self.addr[0]}:{self.addr[1]}"


class UsbSink:
    on_group = False                         # not on the multicast group — flip per board

    def __init__(self, device):
        self.device = device
        self.fd = _open_raw(device)

    def encode(self, frame):
        return frame                         # USB `data` is raw (fast + reliable link)

    def transmit(self, frame):
        self._write(PREFIX + b"data" + frame)

    def send(self, frame):
        self.transmit(self.encode(frame))

    def send_command(self, cmd):
        self._write(PREFIX + cmd.encode())

    def _write(self, buf):
        n = 0
        while n < len(buf):
            n += os.write(self.fd, buf[n:])

    def __str__(self):
        return f"usb:{self.device}"


def make_sink(target, cmd_port=DEFAULT_CMD_PORT, compress=True, repeat=1):
    if target.startswith("udp:"):
        rest = target[4:]
        if ":" in rest:
            ip, port = rest.rsplit(":", 1)
            return UdpSink(ip, int(port), cmd_port, compress, repeat)
        return UdpSink(rest, DEFAULT_UDP_PORT, cmd_port, compress, repeat)
    if target.startswith(("usb:", "serial:")):
        return UsbSink(target.split(":", 1)[1])
    sys.exit(f"error: target must be udp:IP[:port] or usb:/dev/... (got '{target}')")


# ---- media → composite frames ---------------------------------------------------------

def fit(frame_rgb, size, mode):
    w, h = size
    if mode == "stretch":
        return frame_rgb.resize((w, h))
    src = frame_rgb
    sw, sh = src.size
    scale = min(w / sw, h / sh) if mode == "contain" else max(w / sw, h / sh)
    resized = src.resize((max(1, round(sw * scale)), max(1, round(sh * scale))))
    canvas = Image.new("RGB", (w, h), (0, 0, 0))
    canvas.paste(resized, ((w - resized.width) // 2, (h - resized.height) // 2))
    return canvas


# ---- video (ffmpeg) -------------------------------------------------------------------

VIDEO_EXTS = {".mp4", ".mov", ".m4v", ".webm", ".mkv", ".avi", ".mpg", ".mpeg",
              ".flv", ".wmv", ".ts", ".ogv", ".3gp", ".m2ts"}


def is_video(path):
    return os.path.splitext(path)[1].lower() in VIDEO_EXTS


def _scale_filter(size, mode):
    w, h = size
    if mode == "contain":
        return f"scale={w}:{h}:force_original_aspect_ratio=decrease,pad={w}:{h}:(ow-iw)/2:(oh-ih)/2"
    if mode == "cover":
        return f"scale={w}:{h}:force_original_aspect_ratio=increase,crop={w}:{h}"
    return f"scale={w}:{h}"                       # stretch


def probe_fps(path):
    """Native frame rate of a video's first stream, or None if it can't be read."""
    try:
        out = subprocess.run(
            ["ffprobe", "-v", "error", "-select_streams", "v:0",
             "-show_entries", "stream=r_frame_rate", "-of", "default=nk=1:nw=1", path],
            capture_output=True, text=True, check=True).stdout.strip()
        num, _, den = out.partition("/")
        return float(num) / float(den or 1)
    except Exception:
        return None


def video_frames(path, size, mode, fps):
    """Yield composite-sized RGB frames decoded + scaled by ffmpeg. ffmpeg does the fit
    and (re)samples to `fps`, so downstream is identical to the image path."""
    if not shutil_which("ffmpeg"):
        sys.exit("error: video input needs ffmpeg on PATH (brew install ffmpeg)")
    w, h = size
    cmd = ["ffmpeg", "-loglevel", "error", "-i", path,
           "-vf", _scale_filter(size, mode), "-r", f"{fps}",
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    fsize = w * h * 3
    try:
        while True:
            data = proc.stdout.read(fsize)
            if len(data) < fsize:
                break
            yield Image.frombytes("RGB", (w, h), data)
    finally:
        if proc.stdout:
            proc.stdout.close()
        proc.terminate()
        proc.wait()


def shutil_which(prog):
    import shutil
    return shutil.which(prog)


# ---- config ---------------------------------------------------------------------------

def parse_inline_board(spec):
    # TARGET@X,Y+WxH
    if "@" not in spec or "+" not in spec:
        sys.exit(f"error: --board needs TARGET@X,Y+WxH (got '{spec}')")
    target, geom = spec.rsplit("@", 1)
    at, sz = geom.split("+", 1)
    x, y = (int(v) for v in at.split(","))
    w, h = (int(v) for v in sz.lower().split("x"))
    return {"target": target, "at": [x, y], "size": [w, h]}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("media", help="image / animated GIF / WebP / video (mp4/mov/webm/… via ffmpeg)")
    ap.add_argument("--config", help="wall JSON config")
    ap.add_argument("--size", help="composite WxH (inline mode)")
    ap.add_argument("--board", action="append", default=[], help="TARGET@X,Y+WxH (repeatable)")
    ap.add_argument("--fit", choices=["stretch", "contain", "cover"], default=None)
    ap.add_argument("--fps", type=float, default=None, help="override frame rate")
    ap.add_argument("--loop", type=int, default=1, help="times through the animation")
    ap.add_argument("--forever", action="store_true")
    ap.add_argument("--sync", action="store_true",
                    help="S7.4 synced flip: hold boards, unicast each slice, then one "
                         "multicast FLIP presents them together (lockstep, no per-board skew)")
    ap.add_argument("--no-sync", dest="no_sync", action="store_true",
                    help="force live mode even if the config has a sync block")
    ap.add_argument("--guard-ms", dest="guard_ms", type=float, default=None,
                    help="sync mode: ms to wait after slices before the flip (default 12)")
    ap.add_argument("--no-compress", dest="no_compress", action="store_true",
                    help="send raw frames over UDP instead of zlib-compressed")
    ap.add_argument("--repeat", type=int, default=None,
                    help="send each frame N times for loss resilience (board dedups; default 1)")
    ap.add_argument("--flip-repeat", dest="flip_repeat", type=int, default=None,
                    help="sync mode: send the flip N times (default 3)")
    ap.add_argument("--unicast-flip", dest="unicast_flip", action="store_true",
                    help="sync mode: send the flip unicast to each board (avoids AP multicast/"
                         "DTIM buffering delay) instead of one multicast packet")
    args = ap.parse_args()

    if args.config:
        with open(args.config) as fh:
            cfg = json.load(fh)
    else:
        if not args.size or not args.board:
            sys.exit("error: give --config, or --size and one/more --board")
        cfg = {"size": [int(v) for v in args.size.lower().split("x")],
               "boards": [parse_inline_board(b) for b in args.board]}

    size = tuple(cfg["size"])
    mode = args.fit or cfg.get("fit", "stretch")
    fps = args.fps if args.fps is not None else cfg.get("fps")
    repeat = args.repeat if args.repeat is not None else int(cfg.get("repeat", 1))
    # A frame held longer than this is re-sent every `refresh_s` (loss insurance for stills /
    # slow content — a lost packet gets filled within a fraction of a second). Fast frames
    # send once (a rare drop is invisible; the next frame is milliseconds away).
    refresh_s = float(cfg.get("refresh_ms", 200)) / 1000.0

    # S7.4 synced flip: enabled by --sync or a "sync" block in the config.
    sync_cfg = cfg.get("sync") or {}
    sync_on = (args.sync or ("sync" in cfg)) and not args.no_sync
    cmd_port = int(sync_cfg.get("cmd_port", DEFAULT_CMD_PORT))
    flip_group = sync_cfg.get("group", "239.255.0.1")
    flip_port = int(sync_cfg.get("port", DEFAULT_UDP_PORT))
    guard = float(sync_cfg.get("guard_ms", 18)) / 1000.0   # wait for the unicast slice to land +
    if args.guard_ms is not None:                          # decompress (~8ms) before flipping, so
        guard = args.guard_ms / 1000.0                     # neither board's flip gets gated
    flip_repeat = int(sync_cfg.get("flip_repeat", 3))      # multicast flip is unacked — send a few
    if args.flip_repeat is not None:
        flip_repeat = args.flip_repeat

    boards = []
    for b in cfg["boards"]:
        x, y = b["at"]
        w, h = b["size"]
        if x < 0 or y < 0 or x + w > size[0] or y + h > size[1]:
            sys.exit(f"error: board region {b['at']}+{b['size']} is outside the {size} composite")
        boards.append((make_sink(b["target"], cmd_port, not args.no_compress, repeat),
                       (x, y, x + w, y + h)))

    flip_sock = None
    if sync_on and any(s.on_group for s, _ in boards):
        flip_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        flip_sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)

    flip_pkt = MVF1 + struct.pack("<HHII", 0, FLAG_FLIP, 0, 0)
    unicast_flip = args.unicast_flip or bool(sync_cfg.get("unicast_flip"))
    def do_flip():
        if flip_sock is not None:
            for _ in range(flip_repeat):
                if unicast_flip:                           # per-board — dodges AP multicast/DTIM delay
                    for s, _ in boards:
                        if s.on_group:
                            flip_sock.sendto(flip_pkt, (s.ip, flip_port))
                else:                                      # one multicast packet flips every board
                    flip_sock.sendto(flip_pkt, (flip_group, flip_port))
        for s, _ in boards:                                # non-group (USB) boards: flip each
            if not s.on_group:
                s.send_command("flip")

    # Frame source: video → ffmpeg (any format, scaled + resampled by ffmpeg); otherwise
    # Pillow (image / animated GIF / WebP). Both yield (composite_rgb_image, period_seconds),
    # so everything downstream (crop/pack/send/sync) is identical. A fresh generator each
    # call re-opens/re-spawns, so --forever/--loop replays from the top.
    if is_video(args.media):
        vfps = args.fps or probe_fps(args.media) or 25.0
        period_v = 1.0 / vfps
        def make_frames():
            for comp in video_frames(args.media, size, mode, vfps):
                yield comp, period_v
        animated = True
        source_desc = f"video @{vfps:.0f}fps"
    else:
        img = Image.open(args.media)
        n_frames = getattr(img, "n_frames", 1)
        animated = n_frames > 1
        def make_frames():
            for f in ImageSequence.Iterator(img):
                per = (1.0 / fps) if fps else ((f.info.get("duration", 0) or 0) / 1000.0 or (1.0 / 15))
                yield fit(f.convert("RGB"), size, mode), per
        source_desc = f"{n_frames} frame(s)"

    print(f"wall {size[0]}x{size[1]} · {len(boards)} boards · {source_desc} · "
          f"fit={mode}{' · sync' if sync_on else ''}")
    for sink, box in boards:
        print(f"  {sink}  region {box}")

    def sleep_until(deadline):
        dt = deadline - time.perf_counter()
        if dt > 0:
            time.sleep(dt)

    # Deadline scheduler: advance an absolute clock by the frame period and sleep until it,
    # so the *real* rate hits the target regardless of per-frame work (decode + zlib.compress
    # + send take several ms — a naive sleep(1/fps) stacks on top of that and runs slow). In
    # sync mode the flip fires `guard` into the period so the unicast slices land first.
    def play_once(t):
        for comp, period in make_frames():
            # Crop + compress every board's slice up front, BEFORE the flip clock starts, so
            # the timed window is pure transmission — the flip can't race an un-encoded slice.
            encoded = [(sink, sink.encode(pack_penrgb888(comp.crop(box)))) for sink, box in boards]

            def send_and_flip():
                s0 = time.perf_counter()
                for sink, enc in encoded:
                    sink.transmit(enc)
                if sync_on:
                    if guard > 0:
                        sleep_until(s0 + guard)   # let the unicast slices land before the flip
                    do_flip()

            # How long this frame stays up: its own period, or ~1s for a lone still image.
            hold = period if animated else 1.0
            end = t + hold
            send_and_flip()
            # Loss insurance for slow/still content: while the frame is held well past a
            # refresh interval, re-send it (new fid → reload+reflip) so a lost packet gets
            # filled within refresh_s. Fast frames never enter this loop (send once).
            while refresh_s > 0 and time.perf_counter() < end - refresh_s:
                sleep_until(time.perf_counter() + refresh_s)
                send_and_flip()
            if not animated:
                return t                          # single still shown + refreshed; leave it up
            now = time.perf_counter()
            t = end if end >= now else now        # pace to the frame deadline (resync if behind)
            sleep_until(t)
        return t

    for s, _ in boards:                # start from a known present mode: hold for sync (load
        s.send_command("hold" if sync_on else "live")   # then flip), live otherwise (present
                                       # on completion) — also clears any stale hold from a
                                       # previous run that was killed before its cleanup

    loops = 0
    clock = time.perf_counter()
    try:
        while args.forever or loops < args.loop:
            clock = play_once(clock)
            loops += 1
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        if sync_on:
            for s, _ in boards:        # restore immediate mode so plain `data` presents again
                s.send_command("live")


if __name__ == "__main__":
    main()
