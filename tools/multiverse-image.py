#!/usr/bin/env python3
"""Send an image to a Multiverse board as a `data` (raw) or `zdat` (zlib) frame.

Decodes the common still formats (PNG / JPEG / GIF / WebP), fits each frame to
the panel, packs it into the firmware's PenRGB888 framebuffer layout, and writes
the framed bytes to the board's serial device.

Animated GIF/WebP are streamed frame-by-frame and always sent compressed
(`zdat`): the firmware renders every frame it receives immediately, so animation
is driven entirely from the host by pacing frames with their inter-frame delays,
and zlib framing is markedly faster over USB than raw frames. An explicit `data`
request for animated input is overridden to `zdat`; the rare frame that does not
compress below the buffer falls back to a raw `data` frame. Pass a still image
or `--still` to send a single frame, where the requested mode is honoured.

Pixel layout — the firmware's display buffer is a Pimoroni `PicoGraphics`
`PenRGB888` surface. `set_pixel` stores the 32-bit word `(r<<16)|(g<<8)|b`
natively, so on the little-endian RP2040/RP2350 each pixel is four bytes in the
order **B, G, R, 0** (not RGBA). `pack_bgr0()` produces exactly that.

Wire format (see src/command/command_core.cpp):
  data : b"multiverse:data" + W*H*4 raw bytes
  zdat : b"multiverse:zdat" + <uint32 LE compressed size> + zlib stream
         (the firmware rejects a compressed size larger than W*H*4)

Only Pillow is an external dependency; packing and framing use the stdlib so
they can be unit-tested without it.
"""

import argparse
import os
import struct
import sys
import time
import zlib

# Fallback per-frame delay (ms) when an animated frame carries no duration.
DEFAULT_FRAME_MS = 100


def pack_bgr0(rgb: bytes, width: int, height: int) -> bytes:
    """Pack row-major R,G,B bytes into the firmware's B,G,R,0 layout."""
    n = width * height
    if len(rgb) != n * 3:
        raise ValueError(f"expected {n * 3} RGB bytes, got {len(rgb)}")
    out = bytearray(n * 4)
    out[0::4] = rgb[2::3]  # B
    out[1::4] = rgb[1::3]  # G
    out[2::4] = rgb[0::3]  # R
    # out[3::4] stays 0 (unused alpha/pad byte)
    return bytes(out)


def _require_pillow():
    try:
        from PIL import Image
    except ImportError:
        sys.exit(
            "error: Pillow is required to decode images.\n"
            "       install it into the python you run this with, e.g.:\n"
            "         python3 -m pip install Pillow"
        )
    return Image


def fit_rgb(img, width: int, height: int, fit: str, note_label=None) -> bytes:
    """Fit a single PIL image to width x height, return row-major R,G,B bytes."""
    from PIL import Image

    img = img.convert("RGB")
    if img.size != (width, height):
        if fit == "stretch":
            img = img.resize((width, height))
        elif fit == "contain":
            scaled = img.copy()
            scaled.thumbnail((width, height))
            canvas = Image.new("RGB", (width, height), (0, 0, 0))
            canvas.paste(scaled, ((width - scaled.width) // 2,
                                  (height - scaled.height) // 2))
            img = canvas
        elif fit == "cover":
            sw, sh = img.size
            scale = max(width / sw, height / sh)
            img = img.resize((max(1, round(sw * scale)), max(1, round(sh * scale))))
            left = (img.width - width) // 2
            top = (img.height - height) // 2
            img = img.crop((left, top, left + width, top + height))
        if note_label is not None:
            print(f"note: fit {note_label} to {width}x{height} ({fit})",
                  file=sys.stderr)
    return img.tobytes()


def load_frames(path: str, width: int, height: int, fit: str, still: bool):
    """Decode `path` into a list of (packed_bytes, duration_ms) frames.

    Each `packed_bytes` is already in the firmware's B,G,R,0 layout. A still
    image (or `still=True`) yields exactly one frame with duration 0. Animated
    GIF/WebP yield one entry per frame, each carrying that frame's display
    duration in ms (falling back to DEFAULT_FRAME_MS when the file omits it).
    """
    Image = _require_pillow()
    from PIL import ImageSequence

    img = Image.open(path)
    n_frames = getattr(img, "n_frames", 1)
    label = os.path.basename(path)

    if still or n_frames <= 1:
        img.seek(0)  # first frame of an animated GIF/WebP
        rgb = fit_rgb(img, width, height, fit, label)
        return [(pack_bgr0(rgb, width, height), 0)]

    frames = []
    noted = False
    # Iterate in order so Pillow composites disposal/partial frames correctly.
    for frame in ImageSequence.Iterator(img):
        rgb = fit_rgb(frame, width, height, fit, None if noted else label)
        noted = True
        duration = frame.info.get("duration") or DEFAULT_FRAME_MS
        frames.append((pack_bgr0(rgb, width, height), int(duration)))
    return frames


def build_frame(mode: str, packed: bytes, buffer_size: int,
                fallback_to_data: bool = False) -> bytes:
    """Wrap the packed pixels in the `data` or `zdat` wire framing.

    In `zdat` mode a frame that does not compress below `buffer_size` cannot be
    sent (the firmware rejects it): with `fallback_to_data` it is emitted as a
    raw `data` frame instead (used for animation, where aborting the whole
    stream on one incompressible frame is worse), otherwise this exits.
    """
    if mode == "data":
        return b"multiverse:data" + packed
    comp = zlib.compress(packed, 9)
    if len(comp) > buffer_size:
        if fallback_to_data:
            return b"multiverse:data" + packed
        sys.exit(
            f"error: compressed size {len(comp)} B exceeds the firmware limit "
            f"({buffer_size} B); this image does not compress — send it with "
            f"'data' instead."
        )
    return b"multiverse:zdat" + struct.pack("<I", len(comp)) + comp


class RawWriter:
    """A serial/file writer that opens the device once and disables OPOST.

    Reused across many frames so an animation does not pay open/close per frame.
    """

    def __init__(self, device: str):
        self.fd = os.open(device, os.O_WRONLY | os.O_NOCTTY)
        try:
            import termios
            attrs = termios.tcgetattr(self.fd)
            attrs[1] &= ~termios.OPOST  # raw output: no NL->CRNL translation
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        except Exception:
            pass  # not a tty (e.g. writing to a file) — nothing to raw-ify

    def write(self, payload: bytes) -> None:
        mv = memoryview(payload)
        sent = 0
        while sent < len(mv):
            sent += os.write(self.fd, mv[sent:])

    def close(self) -> None:
        try:
            import termios
            termios.tcdrain(self.fd)
        except Exception:
            pass
        os.close(self.fd)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def write_raw(device: str, payload: bytes) -> None:
    """Write a single payload to a serial device with OPOST disabled."""
    with RawWriter(device) as w:
        w.write(payload)


def stream_animation(writer: RawWriter, frames, mode: str, buffer_size: int,
                     loop: int, fps) -> None:
    """Stream pre-packed frames, pacing each by its duration (or --fps)."""
    # Pre-build (and, for zdat, pre-compress) wire frames once so looping is cheap.
    built = []
    for packed, duration_ms in frames:
        payload = build_frame(mode, packed, buffer_size, fallback_to_data=True)
        delay = (1.0 / fps) if fps else (duration_ms / 1000.0)
        built.append((payload, delay))

    forever = loop == 0
    count = 0
    try:
        while forever or count < loop:
            count += 1
            for payload, delay in built:
                start = time.perf_counter()
                writer.write(payload)
                # Subtract send time so the frame rate tracks the source cadence.
                remaining = delay - (time.perf_counter() - start)
                if remaining > 0:
                    time.sleep(remaining)
    except KeyboardInterrupt:
        print("\ninterrupted — stopping animation", file=sys.stderr)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["data", "zdat"],
                    help="data = raw frame, zdat = zlib-compressed frame")
    ap.add_argument("image", help="image file (png/jpg/gif/webp)")
    ap.add_argument("device", help="serial device, e.g. /dev/cu.usbmodem1234")
    ap.add_argument("--width", type=int, default=256)
    ap.add_argument("--height", type=int, default=64)
    ap.add_argument("--fit", choices=["stretch", "contain", "cover"],
                    default="contain",
                    help="how to fit a mismatched image (default: contain)")
    ap.add_argument("--still", action="store_true",
                    help="send only the first frame of an animated image")
    ap.add_argument("--loop", type=int, default=1, metavar="N",
                    help="times to play an animation; 0 = loop forever "
                         "(default: 1)")
    ap.add_argument("--fps", type=float, default=None, metavar="RATE",
                    help="override animation frame rate (ignores GIF timing)")
    args = ap.parse_args()

    if args.fps is not None and args.fps <= 0:
        sys.exit("error: --fps must be positive")

    buffer_size = args.width * args.height * 4
    frames = load_frames(args.image, args.width, args.height, args.fit, args.still)

    if len(frames) == 1:
        packed = frames[0][0]
        frame = build_frame(args.mode, packed, buffer_size)
        write_raw(args.device, frame)
        if args.mode == "data":
            print(f"sent 'multiverse:data' + {len(packed)} B to {args.device}")
        else:
            comp = len(frame) - len(b"multiverse:zdat") - 4
            print(f"sent 'multiverse:zdat' + {comp} B "
                  f"({comp * 100 // buffer_size}% of {buffer_size} B) to {args.device}")
        return

    # Animation is always streamed compressed — zdat is much faster over USB
    # than raw frames — so an explicit 'data' request for animated input is
    # overridden. (Incompressible frames fall back to raw data per-frame.)
    if args.mode != "zdat":
        print("note: animated image — streaming as 'zdat' (compressed)",
              file=sys.stderr)
    mode = "zdat"

    total_ms = sum(d for _, d in frames)
    src_fps = len(frames) * 1000.0 / total_ms if total_ms else 0
    eff_fps = args.fps if args.fps else src_fps
    loop_desc = "forever" if args.loop == 0 else f"{args.loop}x"
    print(f"streaming {len(frames)} '{mode}' frames at ~{eff_fps:.1f} fps "
          f"({loop_desc}) to {args.device}", file=sys.stderr)

    with RawWriter(args.device) as writer:
        stream_animation(writer, frames, mode, buffer_size,
                         args.loop, args.fps)


if __name__ == "__main__":
    main()
