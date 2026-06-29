# E4 — Chip-family build consolidation

**Phase:** 2 · **Depends on:** E3 · **Unlocks:** simpler releases

## Goal

Replace the per-board `MULTIVERSE_BOARD` build with **two images**:
`rp2040-multiverse` and `rp2350-multiverse`. Each image contains the drivers for
the boards in its chip family and selects the active one at runtime (E3).

## Why

With E3 making board selection a runtime concern, the only reason to build four
targets disappears. Two images mean: fewer artifacts, one firmware per board the
owner actually has to flash, and a clean place to compile in (but disable) WiFi.

Current state (`CMakeLists.txt`): `MULTIVERSE_BOARD` chooses the output name, a
`-D` define, and which `src/display/<board>/<board>.cmake` is included. Each
`.cmake` declares the same `display` INTERFACE library but links a different
Pimoroni driver.

## Family mapping

| Image | Boards | Drivers linked |
|-------|--------|----------------|
| `rp2040` | Cosmic, Galactic, i75 (RP2040), Plasma (RP2040) | cosmic_unicorn, galactic_unicorn, hub75, plasma |
| `rp2350` | Stellar, i75W (RP2350), Plasma (RP2350) | stellar_unicorn, hub75, plasma |

(Confirm exact board↔chip assignments against the hardware line-up.)

## Proposed shape

- Replace `MULTIVERSE_BOARD` with a `CHIP_FAMILY` (or platform) selector:
  `rp2040` | `rp2350`, mapping to the Pico SDK `PICO_PLATFORM`/`PICO_BOARD`.
- A per-family `.cmake` includes **all** drivers for that family instead of one.
- Output names `rp2040-multiverse` / `rp2350-multiverse`; both built in CI.
- Keep a compile define for the family so code can branch on chip where genuinely
  necessary (rare — prefer runtime descriptor).

## User stories

### S4.1 — Family-driven CMake
*As a developer, I want to build by chip family so I get two images, not four.*
**Acceptance criteria**
- A single selector picks `rp2040` or `rp2350` and sets the correct SDK platform.
- Each image links all drivers for its family.
- `MULTIVERSE_BOARD` is removed (or aliased with a deprecation note).

### S4.2 — Driver coexistence
*As the firmware, I want multiple drivers linked in one image without conflict.*
**Acceptance criteria**
- Multiple `<board>.cmake` driver sets merge into one image cleanly (no duplicate-symbol/ISR clashes).
- The unified display layer (E3) picks the active driver at runtime.

### S4.3 — CI builds both images
*As a maintainer, I want CI to produce both UF2s.*
**Acceptance criteria**
- CI matrix builds `rp2040` and `rp2350`.
- Release packaging (`install(...)`/CPack) emits both UF2s with clear names.
- README/install docs updated for the two-image model.

## Technical notes

- RP2350 needs the Pico SDK 2.x toolchain bits; the repo already moved to SDK 2.0
  (commits `bba873f`, `5a88a31`). Verify `PICO_PLATFORM=rp2350`/`PICO_BOARD`
  values.
- Hub75 appears in both families — ensure its `.cmake` include is family-agnostic.
- Watch RAM differences: the buffer-sizing decision from E3/S3.4 may differ per
  image.

## Out of scope

- Runtime selection logic (E3).
- WiFi compilation (E7) — though this epic should leave room for a `*-w` variant
  or a single image with WiFi compiled in and toggled via k/v.
