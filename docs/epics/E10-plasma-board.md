# E10 — Plasma board (WS2812)

**Phase:** 2 → 3 · **Depends on:** E9 · **Issue:** [#47](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/47)

## Goal

WS2812 display support so a **Plasma** board renders the same framebuffer and
commands (`data`/`zdat`/`test`) as i75 — the second board type after the descope.

## Why

Plasma is the other board we keep. It's a WS2812/APA102 strip (or grid) rather than
a Hub75 matrix, so it needs its own driver behind the stable `display::` API. Because
it's a plain framebuffer target like i75, everything above the driver (command core,
config store, self-test) is reused unchanged.

## Proposed shape

- A `display::` implementation for Plasma that renders into the usual `PenRGB888`
  framebuffer, then in `update()` walks the pixels into the WS2812 driver.
- Pimoroni's `WS2812` (`drivers/plasma/ws2812.hpp`) has **no `update(PicoGraphics*)`** —
  it's `set_rgb(index, r, g, b)` + `update()`. So the Plasma `update()` is a bridge
  loop; colour order GRB, `DATA = GPIO 15`.
- Treat Plasma as a **linear strip: width 1 × length N**, `length` from the k/v store.

## User stories

### S10.1 — WS2812 display module ([#48](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/48))
*As the firmware, I want a Plasma display driver behind the stable `display::` API.*
- [ ] `display::` impl using `drivers/plasma` WS2812; `update()` walks the framebuffer into `set_rgb` (GRB, `DATA=15`)
- [ ] `data`/`zdat`/`test` render on the strip
- [ ] WS2812 linked into the plasma image (`plasma.cmake`)

### S10.2 — Configurable strip length ([#49](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/49))
*As a board owner, I want to set my Plasma strip length via the k/v store.*
- [ ] Length (width 1 × N) read from the k/v store (S2.5 keys) with validation
- [ ] Framebuffer sized for the configured length

### S10.3 — Plasma builds + verify ([#50](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/50))
*As a maintainer, I want plasma images for both chips.*
- [ ] `plasma-rp2040` + `plasma-rp2350w` targets, board-chip/version stamped
- [ ] On-hardware verify when a Plasma 2350 is available

## Technical notes

- Only WS2812 for the first cut; APA102 (Plasma 2040's `CLK=14`) can follow.
- A serpentine/grid layout mapping is out of scope until a matrix board is on the
  bench — the strip case is linear.

## Out of scope

- The descope/board-identity work (**E9**).
- APA102 support and non-linear (matrix) layouts.
