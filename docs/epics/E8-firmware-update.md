# E8 — Firmware update

**Phase:** 3 (mode command) → 4 (embedded, provisional) · **Depends on:** E1 (command core), E9 (per-board-chip images + identity) · **Unlocks:** —

## Goal

Give the board owner a first-class, safe way to update firmware. Start with a clean **"enter firmware update mode"** command that drops the chip into BOOTSEL/UF2 mass-storage mode, then build toward a **fully embedded update** where a new image is streamed over the command transport, verified, and applied without the owner touching the BOOTSEL button.

## Why

The primitive already exists: the `_usb` command (`src/main.cpp:221-227`) calls `reset_usb_boot(0, 0)`, which reboots into the RP2 ROM bootloader so a `.uf2` can be copied over USB. Today that command is buried in the `main()` if-chain with the others, has a four-byte cryptic id, and gives the host no way to know **which** image to flash (RP2040 vs RP2350, board type, current version).

Two things motivate a dedicated epic:

1. **Two-image world (E4).** Once builds collapse to one image per chip family, flashing the *wrong* family's UF2 is an easy mistake. The board must report its chip family and current version so host tooling can pick the right file — and the ROM bootloader already rejects a mismatched UF2 family id, but a clear pre-flight check is friendlier than a silent no-op.
2. **Owner ergonomics.** A networked board (E7) on a ceiling or in a wall has no reachable BOOTSEL button. An over-the-wire update path is the long-term goal.

## Proposed shape

- A **system command group** (alongside `_rst`/`_usb` from E1's "system" handlers) exposing:
  - `enter update mode` — the formalised `_usb`: show a clear "UPDATE" state on the display, then `reset_usb_boot`.
  - `report version/identity` — returns chip family, board type (from the E2 k/v store), and a firmware build id so the host knows what to flash.
- **Host-side flow** documented: query identity → pick the matching chip-family UF2 → send "enter update mode" → copy the UF2 to the mounted RPI-RP2 drive.
- **Embedded update (Phase 4, provisional):** receive an image over the command core, write it to a second flash region, verify a checksum/signature, then hand off / reboot into it. This needs a flash layout decision and likely a small bootloader or use of the RP2350 boot path; treat as a spike before committing.

## User stories

### S8.1 — Dedicated "enter firmware update mode" command ([#37](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/37))
*As a board owner, I want a clear command that puts the board into firmware upload mode so that I can flash a new image over USB without finding the BOOTSEL button.*
**Acceptance criteria**
- [ ] The existing `_usb` behaviour (`reset_usb_boot(0, 0)`) is ported into the E1 command core as a named system handler.
- [ ] The display shows an unambiguous "firmware update" state before the chip reboots into the bootloader.
- [ ] Behaviour on both chip families (RP2040, RP2350) is verified to land in BOOTSEL/UF2 mode.

### S8.2 — Report firmware identity & version ([#38](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/38))
*As host tooling, I want to query the board's chip family, board type, and firmware build id so that I can select the correct UF2 and avoid flashing the wrong image.*
**Acceptance criteria**
- [x] A command returns at least: chip family + board type (the `board-chip` id, e.g.
  `i75-rp2350`) and a firmware build/version id — the **`vers`** command also reports
  display geometry + buffer size.
- [x] The build id is derived at compile time (e.g. git describe / version constant) and is stable per build.
- [x] Output format documented (u16 length-prefixed ASCII `key=value` lines; see
  `kv_commands.cpp` / `multiverse-config.py`). Surfaced by `multiverse-ctl.sh diag`.

*Delivered by the diagnostics work — `vers` command + `diag` tool. (The k/v store dump
`keys` came along with it.)*

**Groundwork done** (side-step, commit `16264d7`): the build id already exists.
`CMakeLists.txt` derives it from `git describe --tags --always --dirty` and
passes it as the `MULTIVERSE_VERSION` compile definition (alongside
`MULTIVERSE_BOARD`); `src/version.hpp` exposes both as `multiverse::VERSION` /
`multiverse::BOARD` with fallbacks. The i75 boot screen renders it under
"Ready". What remains for this story: the **command** that returns
identity over the transport, the **chip family** + **board type** (board type
needs E2's k/v store), and a **documented output format**. Note the version is
captured at CMake *configure* time, not every build — revisit if a
regenerate-per-build guarantee is needed for "stable per build".

### S8.3 — Host-side update flow & tooling docs ([#39](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/39))
*As a board owner, I want a documented, scripted flow so that updating is "run this and follow the prompt", not a manual UF2 hunt.*
**Acceptance criteria**
- [ ] Docs describe: query identity → choose chip-family UF2 → enter update mode → copy UF2.
- [ ] A reference host script (or extension of existing host tooling) automates the steps where the OS allows mounting the RPI-RP2 drive.
- [ ] The wrong-family case is called out (ROM rejects a mismatched UF2 family id) with the friendly pre-flight check from S8.2.

### S8.4 — Embedded over-the-wire update (provisional, Phase 4) ([#40](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/40))
*As the owner of a hard-to-reach or networked board, I want to push a new image over the transport so that I never need physical access to BOOTSEL.*
**Acceptance criteria**
- [ ] A spike decides the flash layout (single vs A/B / second region) and the apply mechanism for each chip family.
- [ ] A new image can be streamed over the command core, written to flash, and **checksum-verified** before being made active.
- [ ] A failed or interrupted update does not brick the board — the previous image (or bootloader fallback) remains bootable.

## Technical notes

- `reset_usb_boot` and the `rosc`/`save_and_disable_interrupts` dance mirror `_rst`/`_usb` exactly — keep them in the same system command group E1 introduces; don't duplicate the reboot sequence.
- UF2 **family ids** differ per chip (`RP2040` vs `RP2350` / ARM-S / RISC-V). Identity reporting (S8.2) must match what E4's two images are built as.
- The build id and board name already exist at compile time in `src/version.hpp` (`MULTIVERSE_VERSION` / `MULTIVERSE_BOARD`); S8.2's command should report these rather than re-derive them.
- RP2350 has secure-boot / signed-image facilities the RP2040 lacks; the embedded path (S8.4) should note this divergence rather than assume one mechanism.
- Streaming an image over USB CDC reuses the E1 transport; over WiFi (E7) it reuses the same core but must handle datagram loss/ordering for the firmware payload.
- Don't invent a bespoke embedded-update scheme before the S8.4 spike — prefer the SDK/ROM-supported path where one exists.

## Out of scope

- Automatic/unattended update rollout or fleet management.
- Delta/patch updates — full-image only until proven necessary.
- Signing key management and a trust chain (note it for S8.4 on RP2350; don't build it here).
