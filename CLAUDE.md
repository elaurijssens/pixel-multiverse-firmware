# CLAUDE.md

Guidance for working in this repository.

## What this is

Firmware (C++/Pico SDK) plus a host-side Python library/examples for driving
Pimoroni LED display boards (Galactic, Cosmic, Stellar Unicorn, Interstate 75,
Plasma) from a computer over USB serial. A host streams pixel buffers; the board
renders them.

## Layout

- `src/` — firmware. `src/main.cpp` holds the command loop; `src/display/<board>/`
  has one near-duplicate display implementation per board
  (`galactic`, `cosmic`, `stellar`, `i75`).
- `lib/`, `examples/` — host-side Python `multiverse` library and example scripts.
- `tools/` — host helper scripts (see below).
- `docs/` — **design vision and the epic/user-story roadmap** that drives
  development. Start at `docs/README.md`.

## Building

CMake + the Raspberry Pi Pico SDK, one image **per board** selected at compile
time via the `MULTIVERSE_BOARD` env var (`galactic` default; also `cosmic`,
`stellar`, `i75`). Set `PICO_SDK_PATH` to your local pico-sdk checkout. Output
is `${board}-multiverse.uf2`.

```bash
MULTIVERSE_BOARD=galactic cmake -DPICO_SDK_PATH="$PICO_SDK_PATH" -G Ninja -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

Galactic/Cosmic/i75(non-W) are **RP2040**; Stellar and i75 75W are **RP2350**
(separate build trees, e.g. `cmake-build-*-rp2350`). Per-SDK-2.0 timeout-helper
changes live in the USB code — keep them encapsulated.

## Wire protocol (host → board)

The firmware waits for the literal prefix `multiverse:` then reads a **4-byte**
command (`src/main.cpp`). Current commands: `data`, `zdat` (zlib), `note`,
`test` (two ASCII digits `00`–`99` select a self-test pattern — see
`src/display/selftest.hpp`), `put `/`get `/`del ` (E2 config store, length-prefixed
— see `src/config/kv_commands.cpp`), `_rst` (watchdog reboot), `_usb`
(`reset_usb_boot` → BOOTSEL/UF2 mode). Commands are registered with the E1 core
via `command_core::register_command`. Keep command ids 4 bytes for wire
compatibility with existing host tooling.

`tools/multiverse-ctl.sh reset|usb|test|data|zdat|set|get|del|flash|list` sends
control commands and auto-detects the serial port (`set`/`get`/`del` drive the E2
config store via `tools/multiverse-config.py`). `data`/`zdat` send an image
(png/jpg/gif/webp) uncompressed / zlib-compressed via `tools/multiverse-image.py`
(needs Pillow); that helper packs pixels into the firmware's `PenRGB888` byte
order (little-endian `B,G,R,0` per pixel — **not** RGBA). **macOS gotcha:** write
to the `/dev/cu.usbmodem*` (call-out) device, never `/dev/tty.*` (dial-in) — the
tty twin blocks on carrier-detect and fails with `ENXIO`.

## Shipping a change (bench "CI/CD")

There is no CI/CD pipeline. **After finishing a story, fixing a bug, or landing
an out-of-band feature, deploy and verify on real hardware** via
`tools/multiverse-ctl.sh`:

1. If the change adds a board-controllable behaviour, expose it as a
   `multiverse-ctl.sh` subcommand (a new 4-byte wire command + a case in the
   tool) so it can be exercised from the host.
2. Run `tools/multiverse-ctl.sh flash [NN]` (needs `PICO_SDK_PATH`;
   `MV_BUILD_DIR` selects the build tree, default `cmake-build-debug-rp2350`).
   It reconfigures + builds, archives a **version-stamped** copy of the image
   under `dist/` named from `MULTIVERSE_VERSION` (`git describe`), drops the
   board into BOOTSEL, **polls for the UF2 drive to mount and be ready** before
   copying (the mount takes a moment), then runs self-test pattern `NN`
   (default `20`) once the board re-enumerates to confirm it came back healthy.

Do the individual steps by hand only when `flash` can't (e.g. a board with no
`dist/`-style build tree yet); the sequence is build → version-stamped copy →
`_usb` → wait-for-mount → copy `.uf2` → run a self-test.

## Roadmap & how we work

`docs/` is the source of truth for planned work. Epics `E1`–`E8` each have a doc
in `docs/epics/` and a GitHub issue; their user stories are the unit of work and
each has its own issue. Progress lives on the
[Multiverse Firmware Roadmap](https://github.com/users/elaurijssens/projects/1)
GitHub Project board.

- **Acceptance criteria are checkboxes.** When working a story, tick each
  criterion on its GitHub issue only when it is **actually verified** (not
  assumed), and keep the epic doc in sync. **Close a story only when every
  acceptance-criteria box is checked.**
- Keep the `docs/` tree in sync with reality — when an approach changes, update
  the epic; don't leave it stale.
- The roadmap's direction: collapse the per-board duplication into two
  chip-family images (RP2040 / RP2350) with runtime board configuration. Read
  `docs/README.md` before proposing structural changes.

## Conventions

- **Commits:** do not add a `Co-Authored-By` trailer.
- **Branches:** work happens on feature branches (e.g. `feature/i75zlib`); PR to
  `main`. Commit/push only when asked.
- **GitHub:** the `origin` remote uses SSH (`git@github.com:...`); HTTPS pushes
  fail here, and the OAuth token lacks `workflow` scope (can't push commits that
  touch `.github/workflows/`). Use the `gh` CLI for issues/projects.
- When editing the roadmap, match the existing epic-doc structure (Goal, Why,
  Proposed shape, User stories with checkbox AC, Technical notes, Out of scope).
