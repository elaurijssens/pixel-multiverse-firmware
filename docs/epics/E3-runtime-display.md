# E3 — Runtime display abstraction

**Phase:** 2 · **Depends on:** E2 · **Unlocks:** E4, E5, E6

## Goal

Collapse the four duplicated `src/display/<board>/` implementations into **one**
descriptor-driven display layer that selects its driver and dimensions **at
runtime** from the k/v store (E2). This is what makes a single image per chip
family possible.

## Why

The per-board files are ~95% identical. Comparing `cosmic.cpp`, `galactic.cpp`,
`stellar.cpp`, `i75.cpp`, the only real differences are:

- **Driver class:** `CosmicUnicorn` / `GalacticUnicorn` / `StellarUnicorn` /
  `Hub75` (Plasma later).
- **Dimensions:** WIDTH×HEIGHT (fixed for Unicorns; configurable for i75 and
  Plasma).
- **Font:** bitmap5 vs bitmap8.
- **Audio:** Unicorns support synth/sample; i75 has empty stubs.
- **Buffer:** `WIDTH * HEIGHT * 4`, currently a compile-time array.

The `display` namespace API (`init/update/info/play_audio/play_note` + `buffer`)
is already uniform across boards — it just needs one implementation behind it.

## Proposed shape

- A **board descriptor**: `{ driver_kind, width, height, font, has_audio }`.
  Populated at boot from the k/v store; falls back to a safe default if unset.
- A driver abstraction so `init/update/play_*` dispatch to the selected driver.
  Hub75 also needs its DMA-complete ISR wired when selected.
- **Buffer sizing:** since dimensions are runtime, the framebuffer is sized for
  the family maximum **or** allocated once the board is known. Decide per chip
  family (RP2040 has 264 KB RAM; RP2350 has 520 KB) — the largest i75 modes
  (e.g. 256×64×4 = 64 KB; 128×128×4 = 64 KB) must fit.
- PicoGraphics pen format stays RGB888 as today (revisit for memory if needed).

## Configurable dimensions

- **Unicorns (Cosmic/Galactic/Stellar):** dimensions fixed; descriptor hard-codes
  them from board type.
- **Interstate 75/75W:** width and height are independent settings (16…256 /
  16…128, base-2 steps). Read from k/v store keys.
- **Plasma:** width = 1, length configurable. Read length from k/v store.

## User stories

### S3.1 — Board descriptor + selection ([#17](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/17))
*As the firmware, I want to build a board descriptor from the k/v store at boot so I can drive the right hardware.*
**Acceptance criteria**
- [x] Descriptor struct defined; populated from k/v keys (`board`, `width`, `height`, …). — `display::BoardDescriptor` + `describe()` in `src/display/board_config.{hpp,cpp}`.
- [x] Sensible default when keys are absent (documented). — see **Defaults** below; host-tested.
- [x] Invalid/unknown board values fail safe (e.g. show a diagnostic, don't crash). — unparseable `board` keeps the fallback and sets `invalid` (a flag the consuming stage renders); never asserts.

**Defaults (S3.1):** the `board` key selects the board; when **absent**, `describe()`
uses the caller's fallback (the compile-time `multiverse::BOARD`). Dimensions come
from the per-kind table (galactic 53×11, cosmic 32×32, stellar 16×16, i75 256×64,
plasma 1×64); i75/Plasma then override from the `width`/`height` keys (basic
1..256 sanity here; per-axis base-2 validation is S3.5), Unicorns keep fixed dims.
An unparseable `board` value (or an `Unknown` fallback) yields a safe 256×64 bitmap8
surface with `invalid = true` rather than crashing.

### S3.2 — Unified display implementation ([#18](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/18))
*As a developer, I want a single display implementation so there's no per-board copy-paste.*
**Acceptance criteria**
- [ ] One implementation replaces the four `<board>.cpp` files for the shared API.
- [ ] `info()` uses the descriptor's font; `play_*` no-op cleanly when `has_audio` is false.
- [ ] Output is visually identical to the current per-board firmware for each board.

### S3.3 — Driver dispatch (incl. Hub75 ISR) ([#19](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/19))
*As the firmware, I want driver calls routed to the selected driver, including Hub75's DMA ISR.*
**Acceptance criteria**
- [ ] `init/update` dispatch to the active driver.
- [ ] Hub75 path wires `dma_complete` ISR only when Hub75 is selected.
- [ ] Selecting a driver not linked into the current image is impossible or fails gracefully.

### S3.4 — Runtime buffer sizing ([#20](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/20))
*As the firmware, I want the framebuffer sized correctly for the configured dimensions.*
**Acceptance criteria**
- [ ] Buffer accommodates the configured W×H×4 within the chip's RAM budget.
- [ ] The largest supported i75 mode fits on its target chip family.
- [ ] Data commands (`data`/`zdat`) use the runtime size, not a compile-time constant.

### S3.5 — Configurable i75 / Plasma dimensions ([#21](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/21))
*As a board owner, I want to set my i75 or Plasma resolution via the k/v store.*
**Acceptance criteria**
- [ ] i75 width/height and Plasma length read from k/v keys with validation (base-2 ranges).
- [ ] Out-of-range values rejected with a diagnostic and a fallback.

## Technical notes

- The audio API differs only in that i75 stubs are empty — `has_audio=false`
  collapses those to no-ops in one place.
- Which drivers are *linked* is an E4 (CMake) concern; E3 must tolerate "driver
  configured but driver-for-other-family not present."
- Keep the `display::` namespace API stable so `main.cpp`/command handlers don't
  change.
- **Wrong-driver hazard — see [`docs/reference/board-pins.md`](../reference/board-pins.md).**
  Boards reuse overlapping GPIOs, and one board's driver *outputs* land on another
  board's *switch-to-ground inputs* (worst case: Hub75 drives six of the Unicorn's
  button pins). So:
  - **S3.3 driver dispatch** must enforce a **per-image allow-list** — only a board
    whose driver is linked can be selected; anything else fails safe.
  - **A family image has no safe default driver** (conflicting pins). When `board` is
    unset/invalid there, bring up **no driver** — pins high-impedance, render nothing,
    wait for the owner to set `board` over USB. (Today's per-board images link one
    driver, so the S3.1 `multiverse::BOARD` fallback stays safe.) This tightens the
    S3.1 `invalid` path and S3.3's "selecting an unlinked driver fails gracefully" AC.
  - Ship a **user-facing warning not to operate the board switches** on unified
    firmware (the firmware never reads them anyway).
- **Plasma driver integration (prep for S3.3):** unlike the Unicorn/Hub75 drivers,
  Pimoroni's `WS2812` (`drivers/plasma/ws2812.hpp`) has **no `update(PicoGraphics*)`** —
  it's `set_rgb(index, r, g, b)` + `update()`. So the Plasma path renders into the
  usual `PenRGB888` framebuffer, then `display::update()` walks the pixels into
  `ws2812.set_rgb()` (default GRB, `DATA = GPIO 15`). Treat Plasma as a linear strip
  (`width 1 × length N`, `length` from the `height` k/v key). Link the WS2812 driver
  into the RP2350 image so a **Plasma 2350 W** works on arrival (hardware not yet on
  the bench — build/compile-verified only until then).

## Out of scope

- The CMake/image consolidation itself (E4).
- Double buffering (E6) — single visible buffer here.
