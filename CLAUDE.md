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
`_rst` (watchdog reboot), `_usb` (`reset_usb_boot` → BOOTSEL/UF2 mode). Keep
command ids 4 bytes for wire compatibility with existing host tooling.

`tools/multiverse-mode.sh reset|usb|list` sends `_rst`/`_usb` and auto-detects
the serial port. **macOS gotcha:** write to the `/dev/cu.usbmodem*` (call-out)
device, never `/dev/tty.*` (dial-in) — the tty twin blocks on carrier-detect and
fails with `ENXIO`.

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
