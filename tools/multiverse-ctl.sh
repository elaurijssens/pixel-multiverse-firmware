#!/usr/bin/env bash
#
# multiverse-ctl.sh — control a Multiverse display board and flash firmware.
#
# Usage:
#   multiverse-ctl.sh reset [device]       Reboot the firmware          (multiverse:_rst)
#   multiverse-ctl.sh usb   [device]       Reboot into BOOTSEL/UF2 mode (multiverse:_usb)
#   multiverse-ctl.sh factory [device]     Erase config → factory defaults (multiverse:_fac)
#   multiverse-ctl.sh test NN [device]     Show self-test pattern NN    (multiverse:testNN)
#   multiverse-ctl.sh data IMG [device]    Send image uncompressed      (multiverse:data)
#   multiverse-ctl.sh zdat IMG [device]    Send image zlib-compressed   (multiverse:zdat)
#   multiverse-ctl.sh hold|live [device]   Deferred / immediate present (multiverse:hold/live)
#   multiverse-ctl.sh flip [device]        Present the loaded back buffer (multiverse:flip)
#   multiverse-ctl.sh set KEY VAL [device] Set a config key             (multiverse:put )
#   multiverse-ctl.sh get KEY [device]     Read a config key            (multiverse:get )
#   multiverse-ctl.sh del KEY [device]     Delete a config key          (multiverse:del )
#   multiverse-ctl.sh dims WxH [device]    Single panel (1x1), reboot, verify (test 42)
#   multiverse-ctl.sh layout PWxPH CxR [chain] [device]
#                                          Multi-panel grid, reboot, verify (test 60).
#                                          chain: raster-td|serpentine-td|raster-bu|serpentine-bu
#   multiverse-ctl.sh diag [device]        Firmware version + k/v dump (troubleshoot)
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
  sed -n '3,51p' "$0" | sed 's/^# \{0,1\}//'
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
  # WiFi (W) variants build for a _w board (pico2_w) — match the firmware's board id.
  case "$(sed -n 's/^PICO_BOARD[^=]*=//p' "$MV_BUILD_DIR/CMakeCache.txt" | head -n1)" in
    *_w) chip="${chip}w" ;;
  esac

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

# Panel geometry (E11). The firmware reads panel/layout/display and a chain order
# from the k/v store once at boot, so any change needs a reboot to take effect.
# `dims` is the single-panel shortcut; `layout` sets a multi-panel grid. Both
# pre-validate against the firmware's constraints to fail fast, set the keys,
# reboot, and show a self-test to eyeball the result.

# Known chain orders — must match the firmware's parse_chain().
_valid_chain() {
  case "$1" in raster-td|serpentine-td|raster-bu|serpentine-bu) return 0 ;; *) return 1 ;; esac
}

# Validate panel + layout; on success sets DW/DH (display) globals. Exits on error.
_geom_validate() {
  local pw="$1" ph="$2" cols="$3" rows="$4" v
  for v in "$pw" "$ph" "$cols" "$rows"; do
    printf '%s' "$v" | grep -qE '^[0-9]+$' || { echo "error: dimensions must be integers (got '$v')." >&2; exit 1; }
  done
  [ "$pw" -ge 8 ] && [ "$pw" -le 256 ] || { echo "error: panel width must be 8..256 (got $pw)." >&2; exit 1; }
  { [ "$ph" -ge 8 ] && [ "$ph" -le 64 ] && [ $((ph % 2)) -eq 0 ]; } || \
    { echo "error: panel height must be even and 8..64 — hub75 scans ≤ 64 rows (got $ph)." >&2; exit 1; }
  { [ "$cols" -ge 1 ] && [ "$rows" -ge 1 ]; } || { echo "error: layout must be at least 1x1 (got ${cols}x${rows})." >&2; exit 1; }
  DW=$((pw * cols)); DH=$((ph * rows))
  [ $((DW * DH)) -le 16384 ] || { echo "error: display ${DW}x${DH} = $((DW*DH)) px exceeds the 16384 px buffer." >&2; exit 1; }
}

# Set the geometry keys, reboot, and wait for re-enumeration. Sets GEOM_PORT.
# Args: device pw ph cols rows chain dw dh
_geom_apply() {
  local device="$1" pw="$2" ph="$3" cols="$4" rows="$5" chain="$6" dw="$7" dh="$8" i p
  echo "==> panel ${pw}x${ph} · layout ${cols}x${rows} → display ${dw}x${dh} · chain ${chain}"
  config_cmd set "$device" panel_w  "$pw"
  config_cmd set "$device" panel_h  "$ph"
  config_cmd set "$device" panels_x "$cols"
  config_cmd set "$device" panels_y "$rows"
  config_cmd set "$device" disp_w   "$dw"
  config_cmd set "$device" disp_h   "$dh"
  config_cmd set "$device" chain    "$chain"
  echo "==> rebooting to apply"
  send "_rst" "$device"
  echo -n "==> waiting for board to re-enumerate"
  GEOM_PORT=""
  for i in $(seq 1 30); do
    p="$(first_port)"
    [ -n "$p" ] && [ -e "$p" ] && { GEOM_PORT="$p"; break; }
    echo -n "."; sleep 1
  done
  echo
}

# dims WxH [device] — single panel (1×1 layout); shows the dimensions test (42).
dims() {
  local spec="${1:-}" device="${2:-$(first_port)}"
  case "$spec" in *x*) : ;; *) echo "error: dims needs WxH, e.g. 256x64 (got '${spec}')." >&2; exit 1 ;; esac
  _geom_validate "${spec%%x*}" "${spec##*x}" 1 1
  [ -n "$device" ] || { echo "error: no Multiverse serial port found (plug in or pass a device)." >&2; exit 1; }
  _geom_apply "$device" "${spec%%x*}" "${spec##*x}" 1 1 raster-td "$DW" "$DH"
  if [ -z "$GEOM_PORT" ]; then
    echo "warning: board did not re-enumerate; once back, run: $(basename "$0") test 42" >&2; exit 1
  fi
  sleep 1  # let the CDC endpoint settle
  echo "==> showing dimensions (test 42)"
  send "test42" "$GEOM_PORT"
  echo "==> done: ${DW}x${DH}"
}

# layout PWxPH COLSxROWS [chain] [device] — multi-panel grid; shows the layout map (60).
layout() {
  local panel="${1:-}" grid="${2:-}" chain="${3:-raster-td}" device="${4:-$(first_port)}"
  case "$panel" in *x*) : ;; *) echo "error: layout needs PWxPH COLSxROWS, e.g. 128x64 1x2 (got panel '${panel}')." >&2; exit 1 ;; esac
  case "$grid"  in *x*) : ;; *) echo "error: layout needs PWxPH COLSxROWS, e.g. 128x64 1x2 (got layout '${grid}')." >&2; exit 1 ;; esac
  _valid_chain "$chain" || { echo "error: chain must be raster-td|serpentine-td|raster-bu|serpentine-bu (got '${chain}')." >&2; exit 1; }
  _geom_validate "${panel%%x*}" "${panel##*x}" "${grid%%x*}" "${grid##*x}"
  [ -n "$device" ] || { echo "error: no Multiverse serial port found (plug in or pass a device)." >&2; exit 1; }
  _geom_apply "$device" "${panel%%x*}" "${panel##*x}" "${grid%%x*}" "${grid##*x}" "$chain" "$DW" "$DH"
  if [ -z "$GEOM_PORT" ]; then
    echo "warning: board did not re-enumerate; once back, run: $(basename "$0") test 60" >&2; exit 1
  fi
  sleep 1  # let the CDC endpoint settle
  echo "==> showing layout map (test 60)"
  send "test60" "$GEOM_PORT"
  echo "==> done: ${grid} of ${panel} → ${DW}x${DH} (${chain})"
}

# diag [device] — firmware identity (vers) + k/v store dump (keys), for troubleshooting.
diag() {
  local device="${1:-$(first_port)}"
  if [ -z "$device" ]; then
    echo "error: no Multiverse serial port found (plug in or pass a device)." >&2
    detect_ports | sed 's/^/         /' >&2
    exit 1
  fi
  [ -e "$device" ] || { echo "error: device '$device' does not exist." >&2; exit 1; }

  echo "== Multiverse diagnostics =="
  echo "port:  $device"
  echo
  echo "-- firmware --"
  "${MV_PYTHON:-python3}" "$SCRIPT_DIR/multiverse-config.py" vers --device "$device" 2>&1 | sed 's/^/  /'
  echo
  echo "-- config store --"
  local dump
  dump="$("${MV_PYTHON:-python3}" "$SCRIPT_DIR/multiverse-config.py" keys --device "$device" 2>&1)"
  if [ -n "$dump" ]; then printf '%s\n' "$dump" | sed 's/^/  /'; else echo "  (store empty)"; fi
}

case "${1:-}" in
  reset|rst|_rst) send "_rst" "${2:-$(first_port)}" ;;
  usb|bootsel|_usb) send "_usb" "${2:-$(first_port)}" ;;
  factory|_fac) send "_fac" "${2:-$(first_port)}" ;;
  test)
    validate_test_id "${2:-}"
    send "test${2}" "${3:-$(first_port)}"
    ;;
  data|zdat) send_image "$1" "${2:-}" "${3:-$(first_port)}" ;;
  flip) send "flip" "${2:-$(first_port)}" ;;
  hold) send "hold" "${2:-$(first_port)}" ;;
  live) send "live" "${2:-$(first_port)}" ;;
  set) config_cmd set "${4:-$(first_port)}" "${2:-}" "${3-}" ;;
  get) config_cmd get "${3:-$(first_port)}" "${2:-}" ;;
  del) config_cmd del "${3:-$(first_port)}" "${2:-}" ;;
  dims) dims "${2:-}" "${3:-}" ;;
  layout) layout "${2:-}" "${3:-}" "${4:-}" "${5:-}" ;;
  diag|diagnostics) diag "${2:-}" ;;
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
