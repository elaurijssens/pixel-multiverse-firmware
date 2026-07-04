#!/usr/bin/env bash
#
# multiverse-ctl.sh — control a Multiverse display board and flash firmware.
#
# Usage:
#   multiverse-ctl.sh reset [device]       Reboot the firmware          (multiverse:_rst)
#   multiverse-ctl.sh usb   [device]       Reboot into BOOTSEL/UF2 mode (multiverse:_usb)
#   multiverse-ctl.sh test NN [device]     Show self-test pattern NN    (multiverse:testNN)
#   multiverse-ctl.sh data IMG [device]    Send image uncompressed      (multiverse:data)
#   multiverse-ctl.sh zdat IMG [device]    Send image zlib-compressed   (multiverse:zdat)
#   multiverse-ctl.sh set KEY VAL [device] Set a config key             (multiverse:put )
#   multiverse-ctl.sh get KEY [device]     Read a config key            (multiverse:get )
#   multiverse-ctl.sh del KEY [device]     Delete a config key          (multiverse:del )
#   multiverse-ctl.sh dims WxH [device]    Set panel size, reboot, verify (test 42)
#   multiverse-ctl.sh flash [NN] [device]  Build, version-stamp, flash, then self-test
#   multiverse-ctl.sh list                 List detected Multiverse ports
#
# If [device] is omitted, the first detected board is used. "flash" runs the
# whole bench-release flow (see the flash() function): rebuild so the embedded
# MULTIVERSE_VERSION is fresh, archive a version-stamped .uf2 under dist/, drop
# the board into BOOTSEL, wait for the drive, copy the image, then run self-test
# pattern NN (default 20) once the board re-enumerates.
#
# "data"/"zdat" accept png/jpg/gif/webp (via tools/multiverse-image.py, needs
# Pillow). Panel size defaults to MV_SIZE (256x64) and must match the firmware;
# MV_FIT (contain|cover|stretch) controls how a mismatched image is fitted.
# An animated GIF/WebP is streamed frame-by-frame and always sent compressed
# ("zdat", regardless of the data/zdat subcommand): MV_LOOP sets how many times
# it plays (0 = forever), MV_FPS overrides the frame rate, MV_STILL=1 sends only
# the first frame (honouring the chosen data/zdat mode).
#
# Env for "flash":  PICO_SDK_PATH (required)   MV_BUILD_DIR (default below)
# Env for images:   MV_SIZE (default 256x64)   MV_FIT (default contain)
#                   MV_LOOP (default 1, 0=forever)   MV_FPS (override rate)
#                   MV_STILL (1 = first frame only)
#                   MV_PYTHON (python with Pillow; defaults to the repo's
#                              .venv/bin/python if present, else python3)
#
# Wire protocol (see src/main.cpp): the firmware waits for the literal prefix
# "multiverse:" then reads a 4-byte command. "_rst" does a watchdog reboot;
# "_usb" calls reset_usb_boot() so a .uf2 can be flashed over USB.
#
# macOS note: always talk to the /dev/cu.* (call-out) device, never /dev/tty.*
# (dial-in) — the tty.* twin blocks on carrier-detect and fails with ENXIO.

set -euo pipefail

PREFIX="multiverse:"
MV_BUILD_DIR="${MV_BUILD_DIR:-cmake-build-debug-rp2350}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Prefer the repo's .venv (where Pillow lives) unless the caller overrides it.
if [ -z "${MV_PYTHON:-}" ] && [ -x "$REPO_ROOT/.venv/bin/python" ]; then
  MV_PYTHON="$REPO_ROOT/.venv/bin/python"
fi

usage() {
  sed -n '3,43p' "$0" | sed 's/^# \{0,1\}//'
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

# Print the path of a mounted RP2040/RP2350 BOOTSEL drive that is ready to
# accept a .uf2 (INFO_UF2.TXT present), or nothing if none is ready.
bootsel_volume() {
  local u="${USER:-$(id -un)}"
  local d
  for d in /Volumes/RP2350 /Volumes/RPI-RP2 \
           "/media/$u/RP2350" "/media/$u/RPI-RP2" \
           "/run/media/$u/RP2350" "/run/media/$u/RPI-RP2"; do
    if [ -f "$d/INFO_UF2.TXT" ]; then echo "$d"; return 0; fi
  done
  return 1
}

validate_test_id() {
  case "$1" in
    [0-9][0-9]) return 0 ;;
    *) echo "error: self-test id must be two digits (00-99), got '$1'" >&2; exit 1 ;;
  esac
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

# Decode an image and stream it as a data/zdat frame via the Python helper.
send_image() {
  local mode="$1" image="$2" device="$3"
  if [ -z "$image" ]; then
    echo "error: '$mode' needs an image file (png/jpg/gif/webp)." >&2
    exit 1
  fi
  if [ ! -f "$image" ]; then
    echo "error: image '$image' not found." >&2
    exit 1
  fi
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

  local size="${MV_SIZE:-256x64}"
  local w="${size%%x*}" h="${size##*x}"
  local extra=()
  [ -n "${MV_LOOP:-}" ] && extra+=(--loop "$MV_LOOP")
  [ -n "${MV_FPS:-}" ] && extra+=(--fps "$MV_FPS")
  [ -n "${MV_STILL:-}" ] && extra+=(--still)
  "${MV_PYTHON:-python3}" "$SCRIPT_DIR/multiverse-image.py" \
    "$mode" "$image" "$device" --width "$w" --height "$h" --fit "${MV_FIT:-contain}" \
    ${extra[@]+"${extra[@]}"}
}

# Read/write a persistent config key via the Python helper.
#   config_cmd set|get|del <device> <key> [value]
config_cmd() {
  local mode="$1" device="$2" key="$3" value="${4-}"
  if [ -z "$key" ]; then
    echo "error: '$mode' needs a key." >&2
    exit 1
  fi
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

  if [ "$mode" = "set" ]; then
    "${MV_PYTHON:-python3}" "$SCRIPT_DIR/multiverse-config.py" set --device "$device" "$key" "$value"
  else
    "${MV_PYTHON:-python3}" "$SCRIPT_DIR/multiverse-config.py" "$mode" --device "$device" "$key"
  fi
}

# Bench-release flow: rebuild, archive a version-stamped image, flash it over
# BOOTSEL, then confirm the board is alive with a self-test pattern.
flash() {
  local test_id="${1:-20}" device="${2:-}"
  validate_test_id "$test_id"

  : "${PICO_SDK_PATH:?set PICO_SDK_PATH to your pico-sdk checkout for 'flash'}"
  # Resolve a relative build dir against the repo, not the caller's cwd.
  case "$MV_BUILD_DIR" in
    /*) : ;;
    *) MV_BUILD_DIR="$REPO_ROOT/$MV_BUILD_DIR" ;;
  esac
  if [ ! -f "$MV_BUILD_DIR/CMakeCache.txt" ]; then
    echo "error: '$MV_BUILD_DIR' is not a configured build dir (set MV_BUILD_DIR)." >&2
    exit 1
  fi

  local board chip version uf2 stamped
  board="$(sed -n 's/^MULTIVERSE_BOARD[^=]*=//p' "$MV_BUILD_DIR/CMakeCache.txt" | head -n1)"
  [ -n "$board" ] || { echo "error: could not read MULTIVERSE_BOARD from cache." >&2; exit 1; }
  # Chip family, so RP2040 and RP2350 images (e.g. i75 vs i75w) don't collide in
  # dist/. PICO_PLATFORM is rp2040 or rp2350-arm-s; keep just rp2040 / rp2350.
  chip="$(sed -n 's/^PICO_PLATFORM[^=]*=//p' "$MV_BUILD_DIR/CMakeCache.txt" | head -n1)"
  case "$chip" in rp2350*) chip=rp2350 ;; rp2040*) chip=rp2040 ;; *) chip="${chip:-unknown}" ;; esac

  # 1. Reconfigure + build so the embedded MULTIVERSE_VERSION is captured NOW
  #    (CMake derives it from git describe at configure time, not every build).
  echo "==> building '$board' ($chip) in $MV_BUILD_DIR"
  cmake -S "$REPO_ROOT" -B "$MV_BUILD_DIR" -DPICO_SDK_PATH="$PICO_SDK_PATH" >/dev/null
  cmake --build "$MV_BUILD_DIR"

  # 2. Version-stamp: same source of truth CMake uses, so the filename matches
  #    the string the board reports on its boot screen. Include the chip family
  #    so 2040/2350 images stay distinct in dist/.
  version="$(git -C "$REPO_ROOT" describe --tags --always --dirty 2>/dev/null || echo unknown)"
  uf2="$MV_BUILD_DIR/${board}-multiverse.uf2"
  [ -f "$uf2" ] || { echo "error: built image '$uf2' not found." >&2; exit 1; }
  mkdir -p "$REPO_ROOT/dist"
  stamped="$REPO_ROOT/dist/${board}-${chip}-multiverse-${version}.uf2"
  cp "$uf2" "$stamped"
  echo "==> archived $stamped"

  # 3. Drop the running board into BOOTSEL/UF2 mode.
  echo "==> requesting BOOTSEL mode"
  send "_usb" "${device:-$(first_port)}"

  # 4. Wait for the mass-storage drive to mount and become ready.
  echo -n "==> waiting for BOOTSEL drive"
  local vol="" i
  for i in $(seq 1 30); do
    vol="$(bootsel_volume || true)"
    [ -n "$vol" ] && break
    echo -n "."; sleep 1
  done
  echo
  [ -n "$vol" ] || { echo "error: BOOTSEL drive did not appear within 30s." >&2; exit 1; }
  echo "==> drive ready at $vol"

  # 5. Copy the image; the board reboots itself once the write completes.
  echo "==> flashing $(basename "$stamped")"
  cp "$stamped" "$vol/"
  sync

  # 6. Wait for the board to re-enumerate as a serial port, then self-test.
  echo -n "==> waiting for board to re-enumerate"
  local port=""
  for i in $(seq 1 30); do
    port="$(first_port)"
    [ -n "$port" ] && [ -e "$port" ] && break
    port=""
    echo -n "."; sleep 1
  done
  echo
  if [ -z "$port" ]; then
    echo "warning: board did not re-enumerate in time." >&2
    echo "         once it's back, run: $(basename "$0") test ${test_id}" >&2
    exit 1
  fi
  sleep 1  # let the CDC endpoint settle before writing
  echo "==> running self-test ${test_id}"
  send "test${test_id}" "$port"
  echo "==> done: ${board} @ ${version}"
}

# Set panel width+height in the config store, reboot so the new geometry takes
# effect (the framebuffer + hub75 driver are built once in init(), so dimensions
# are read only at boot), then show test 42 to eyeball it (border flush to the
# edges + "WxH" text). Firmware clamps width to 16..256 and height to 16..64
# (hub75 scans at most 64 rows); we pre-validate to fail fast.
dims() {
  local spec="${1:-}" device="${2:-$(first_port)}"
  local w="${spec%%x*}" h="${spec##*x}"
  if ! printf '%s' "$w" | grep -qE '^[0-9]+$' || ! printf '%s' "$h" | grep -qE '^[0-9]+$'; then
    echo "error: dims needs WxH, e.g. 256x64 (got '${spec}')." >&2
    exit 1
  fi
  if [ "$w" -lt 16 ] || [ "$w" -gt 256 ] || [ "$h" -lt 16 ] || [ "$h" -gt 64 ]; then
    echo "error: width must be 16..256 and height 16..64 (got ${w}x${h})." >&2
    exit 1
  fi
  if [ -z "$device" ]; then
    echo "error: no Multiverse serial port found." >&2
    echo "       plug the board in, or pass the device explicitly. Detected ports:" >&2
    detect_ports | sed 's/^/         /' >&2
    exit 1
  fi

  echo "==> setting width=${w} height=${h}"
  config_cmd set "$device" width  "$w"
  config_cmd set "$device" height "$h"

  echo "==> rebooting to apply"
  send "_rst" "$device"

  echo -n "==> waiting for board to re-enumerate"
  local port="" i
  for i in $(seq 1 30); do
    port="$(first_port)"
    [ -n "$port" ] && [ -e "$port" ] && break
    port=""
    echo -n "."; sleep 1
  done
  echo
  if [ -z "$port" ]; then
    echo "warning: board did not re-enumerate in time." >&2
    echo "         once it's back, run: $(basename "$0") test 42" >&2
    exit 1
  fi
  sleep 1  # let the CDC endpoint settle before writing
  echo "==> showing dimensions (test 42)"
  send "test42" "$port"
  echo "==> done: ${w}x${h}"
}

case "${1:-}" in
  reset|rst|_rst) send "_rst" "${2:-$(first_port)}" ;;
  usb|bootsel|_usb) send "_usb" "${2:-$(first_port)}" ;;
  test)
    validate_test_id "${2:-}"
    send "test${2}" "${3:-$(first_port)}"
    ;;
  data|zdat) send_image "$1" "${2:-}" "${3:-$(first_port)}" ;;
  set) config_cmd set "${4:-$(first_port)}" "${2:-}" "${3-}" ;;
  get) config_cmd get "${3:-$(first_port)}" "${2:-}" ;;
  del) config_cmd del "${3:-$(first_port)}" "${2:-}" ;;
  dims) dims "${2:-}" "${3:-}" ;;
  flash) flash "${2:-20}" "${3:-}" ;;
  list|ls)
    ports="$(detect_ports)"
    if [ -z "$ports" ]; then echo "no Multiverse ports detected"; exit 0; fi
    if [ "$(uname -s)" = "Darwin" ]; then
      # Map each call-out device to its USB Product Name (e.g. "Multiverse i75-rp2350"),
      # so we can label the port with the board id the firmware now advertises (S9.3).
      map="$(ioreg -rl -c IOUSBHostDevice 2>/dev/null | awk '
        /USB Product Name/ { i=index($0,"= \""); p=substr($0,i+3); sub(/".*/,"",p); prod=p }
        /IOCalloutDevice/  { i=index($0,"= \""); d=substr($0,i+3); sub(/".*/,"",d); print d"\t"prod }')"
      printf '%s\n' "$ports" | while IFS= read -r p; do
        board="$(printf '%s\n' "$map" | awk -F'\t' -v d="$p" '$1==d{print $2; exit}')"
        board="${board#Multiverse }"; board="${board#Multiverse}"
        printf '  %s  %s\n' "$p" "${board:-?}"
      done
    else
      printf '%s\n' "$ports"
    fi
    ;;
  -h|--help|help|"") usage 0 ;;
  *) echo "error: unknown command '$1'" >&2; usage 1 ;;
esac
