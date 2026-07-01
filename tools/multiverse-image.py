#!/usr/bin/env python3
"""Send an image to a Multiverse board as a `data` (raw) or `zdat` (zlib) frame.

Decodes the common still formats (PNG / JPEG / GIF / WebP — the first frame of
an animated GIF/WebP), fits it to the panel, packs it into the firmware's
PenRGB888 framebuffer layout, and writes the framed bytes to the board's serial
device.

Pixel layout — the firmware's display buffer is a Pimoroni `PicoGraphics`
`PenRGB888` surface. `set_pixel` stores the 32-bit word `(r<<16)|(g<<8)|b`
natively, so on the little-endian RP2040/RP2350 each pixel is four bytes in the
order **B, G, R, 0** (not RGBA). `pack_bgr0()` produces exactly that.

Wire format (see src/command/command_core.cpp):
  data : b"multiverse:data" + W*H*4 raw bytes
  zdat : b"multiverse:zdat" + <uint32 LE compressed size> + zlib stream
         (the firmware rejects a compressed size larger than W*H*4)

Only Pillow is an external dependency; packing and framing use the stdlib so
they can be unit-tested without it (see --pack-selftest).
"""

import argparse
import os
import struct
import sys
import zlib


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


def load_rgb(path: str, width: int, height: int, fit: str) -> bytes:
    """Decode `path`, fit it to width x height, return row-major R,G,B bytes."""
    try:
        from PIL import Image
    except ImportError:
        sys.exit(
            "error: Pillow is required to decode images.\n"
            "       install it into the python you run this with, e.g.:\n"
            "         python3 -m pip install Pillow"
        )

    img = Image.open(path)
    img.seek(0)  # first frame of an animated GIF/WebP
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
        print(f"note: fit {os.path.basename(path)} to {width}x{height} ({fit})",
              file=sys.stderr)

    return img.tobytes()


def build_frame(mode: str, packed: bytes, buffer_size: int) -> bytes:
    """Wrap the packed pixels in the `data` or `zdat` wire framing."""
    if mode == "data":
        return b"multiverse:data" + packed
    comp = zlib.compress(packed, 9)
    if len(comp) > buffer_size:
        sys.exit(
            f"error: compressed size {len(comp)} B exceeds the firmware limit "
            f"({buffer_size} B); this image does not compress — send it with "
            f"'data' instead."
        )
    return b"multiverse:zdat" + struct.pack("<I", len(comp)) + comp


def write_raw(device: str, payload: bytes) -> None:
    """Write payload to a serial device with output post-processing disabled."""
    fd = os.open(device, os.O_WRONLY | os.O_NOCTTY)
    try:
        try:
            import termios
            attrs = termios.tcgetattr(fd)
            attrs[1] &= ~termios.OPOST  # raw output: no NL->CRNL translation
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
        except Exception:
            pass  # not a tty (e.g. writing to a file) — nothing to raw-ify
        mv = memoryview(payload)
        sent = 0
        while sent < len(mv):
            sent += os.write(fd, mv[sent:])
        try:
            import termios
            termios.tcdrain(fd)
        except Exception:
            pass
    finally:
        os.close(fd)


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
    args = ap.parse_args()

    buffer_size = args.width * args.height * 4
    rgb = load_rgb(args.image, args.width, args.height, args.fit)
    packed = pack_bgr0(rgb, args.width, args.height)
    frame = build_frame(args.mode, packed, buffer_size)
    write_raw(args.device, frame)

    if args.mode == "data":
        print(f"sent 'multiverse:data' + {len(packed)} B to {args.device}")
    else:
        comp = len(frame) - len(b"multiverse:zdat") - 4
        print(f"sent 'multiverse:zdat' + {comp} B "
              f"({comp * 100 // buffer_size}% of {buffer_size} B) to {args.device}")


if __name__ == "__main__":
    main()
