#!/usr/bin/env bash
#
# multiverse-mode.sh — send a control command to a Multiverse display board.
#
# Usage:
#   multiverse-mode.sh reset [device]   Reboot the firmware            (multiverse:_rst)
#   multiverse-mode.sh usb   [device]   Reboot into BOOTSEL/UF2 mode   (multiverse:_usb)
#   multiverse-mode.sh list             List detected Multiverse ports
#
# If [device] is omitted, the first detected board is used.
#
# Wire protocol (see src/main.cpp): the firmware waits for the literal prefix
# "multiverse:" then reads a 4-byte command. "_rst" does a watchdog reboot;
# "_usb" calls reset_usb_boot() so a .uf2 can be flashed over USB.
#
# macOS note: always talk to the /dev/cu.* (call-out) device, never /dev/tty.*
# (dial-in) — the tty.* twin blocks on carrier-detect and fails with ENXIO.

set -euo pipefail

PREFIX="multiverse:"

usage() {
  sed -n '3,16p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

# Print every candidate Multiverse serial port, most-specific first.
detect_ports() {
  case "$(uname -s)" in
    Darwin)
      # call-out devices only; never the dial-in tty.* twins
      ls /dev/cu.usbmodem* 2>/dev/null || true
      ;;
    Linux)
      # stable by-id symlinks (match Pimoroni Multiverse), then generic CDC ACM
      ls /dev/serial/by-id/*[Mm]ultiverse* 2>/dev/null || true
      ls /dev/ttyACM* 2>/dev/null || true
      ;;
    *)
      ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null || true
      ;;
  esac
}

first_port() {
  detect_ports | head -n1
}

send() {
  local cmd="$1" device="$2"
  if [ -z "$device" ]; then
    echo "error: no Multiverse serial port found." >&2
    echo "       plug the board in, or pass the device explicitly. Detected ports:" >&2
    detect_ports | sed 's/^/         /' >&2
    exit 1
  fi
  if [ ! -e "$device" ]; then
    echo "error: device '$device' does not exist." >&2
    exit 1
  fi

  # Put the port in raw mode so bytes pass through untouched (baud is ignored by USB CDC).
  if [ "$(uname -s)" = "Darwin" ]; then
    stty -f "$device" raw -echo 2>/dev/null || true
  else
    stty -F "$device" raw -echo 2>/dev/null || true
  fi

  printf '%s%s' "$PREFIX" "$cmd" > "$device"
  echo "sent '${PREFIX}${cmd}' to ${device}"
}

case "${1:-}" in
  reset|rst|_rst) send "_rst" "${2:-$(first_port)}" ;;
  usb|bootsel|_usb) send "_usb" "${2:-$(first_port)}" ;;
  list|ls)
    ports="$(detect_ports)"
    if [ -z "$ports" ]; then echo "no Multiverse ports detected"; else echo "$ports"; fi
    ;;
  -h|--help|help|"") usage 0 ;;
  *) echo "error: unknown command '$1'" >&2; usage 1 ;;
esac
