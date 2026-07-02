#!/usr/bin/env python3
"""Read/write the Multiverse board's persistent config over USB serial (E2 S2.4).

Sends a length-prefixed `put `/`get `/`del ` command and reads the board's
status-first reply. Stdlib only (no pyserial): the serial device is opened raw and
driven with os/termios/select.

Wire protocol (see src/config/kv_commands.cpp):
  put : klen(1) key[klen] vlen(1) value[vlen] -> status(1)   (1 ok / 0 fail)
  get : klen(1) key[klen]                      -> 1, vlen(1), value[vlen]  |  0
  del : klen(1) key[klen]                      -> status(1)   (1 deleted / 0 absent)
"""

import argparse
import os
import select
import sys
import termios

PREFIX = b"multiverse:"
KEY_MAX = 8
VALUE_MAX = 64


def open_raw(device):
    fd = os.open(device, os.O_RDWR | os.O_NOCTTY)
    try:
        attrs = termios.tcgetattr(fd)
        # cfmakeraw equivalent: no translation/echo/canonical processing.
        attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP |
                      termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
        attrs[1] &= ~termios.OPOST
        attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIFLUSH)  # drop any stale input
    except termios.error:
        pass  # not a tty (e.g. a pipe in tests) — carry on
    return fd


def read_exact(fd, n, timeout=2.0):
    """Read exactly n bytes, or return what arrived before the timeout."""
    out = bytearray()
    while len(out) < n:
        r, _, _ = select.select([fd], [], [], timeout)
        if not r:
            break
        chunk = os.read(fd, n - len(out))
        if not chunk:
            break
        out += chunk
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["set", "get", "del"])
    ap.add_argument("--device", required=True)
    ap.add_argument("key")
    ap.add_argument("value", nargs="?")
    args = ap.parse_args()

    key = args.key.encode()
    if not (1 <= len(key) <= KEY_MAX):
        sys.exit(f"error: key must be 1..{KEY_MAX} bytes, got {len(key)}")

    if args.mode == "set":
        if args.value is None:
            sys.exit("error: 'set' needs a value")
        value = args.value.encode()
        if len(value) > VALUE_MAX:
            sys.exit(f"error: value must be 0..{VALUE_MAX} bytes, got {len(value)}")
        req = PREFIX + b"put " + bytes([len(key)]) + key + bytes([len(value)]) + value
    elif args.mode == "get":
        req = PREFIX + b"get " + bytes([len(key)]) + key
    else:  # del
        req = PREFIX + b"del " + bytes([len(key)]) + key

    fd = open_raw(args.device)
    try:
        os.write(fd, req)
        try:
            termios.tcdrain(fd)
        except termios.error:
            pass

        status = read_exact(fd, 1)
        if not status:
            sys.exit("error: no response from board (timeout)")

        if args.mode == "get":
            if status[0] != 1:
                print("not found")
                return
            vlen = read_exact(fd, 1)
            if not vlen:
                sys.exit("error: truncated response (no length)")
            value = read_exact(fd, vlen[0])
            try:
                text = value.decode("utf-8")
                printable = all(32 <= ord(c) < 127 for c in text)
            except UnicodeDecodeError:
                printable = False
            print(text if printable else value.hex())
        elif args.mode == "set":
            print("ok" if status[0] == 1 else "failed")
        else:  # del
            print("deleted" if status[0] == 1 else "not found")
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
