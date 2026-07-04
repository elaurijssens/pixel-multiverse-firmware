# E10 — Plasma board (WS2812)

**Phase:** 2 → 3 · **Depends on:** E9 · **Issue:** [#47](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/47)

> **Status:** done — S10.1–S10.3 landed; plasma builds in CI for both chips and a
> Plasma 2350 drives a 66-LED WS2812 strip on the bench (colour order configurable
> via the `order` key; the bench strip is BGR).

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
  loop; `DATA = GPIO 15` (same on Plasma 2040 and 2350). Colour order is a k/v `order`
  key (default GRB) — some strips are RGB/BGR/etc.
- Treat Plasma as a **linear strip: width N × 1**, `length` from the k/v store; the
  strip latches, so frames are pushed on demand (no background refresh).

## User stories

### S10.1 — WS2812 display module ([#48](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/48))
*As the firmware, I want a Plasma display driver behind the stable `display::` API.*
- [x] `display::` impl using `drivers/plasma` WS2812; `update()` walks the framebuffer into `set_rgb` (`DATA=15`, on-demand push — no background refresh)
- [x] `data`/`zdat`/`test` render on the strip (verified: R/G/B fills + a `data` rainbow)
- [x] WS2812 linked into the plasma image (`plasma.cmake`)
- [x] Wire colour order is a k/v `order` key (default GRB), since not all strips are GRB — the bench strip is BGR

### S10.2 — Configurable strip length ([#49](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/49))
*As a board owner, I want to set my Plasma strip length via the k/v store.*
- [x] Length read from the k/v `length` key (1…MAX_LEDS, default 64) with validation + fallback
- [x] `width()`/`buffer_size()` report the configured length (framebuffer static at MAX_LEDS)

### S10.3 — Plasma builds + verify ([#50](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/50))
*As a maintainer, I want plasma images for both chips.*
- [x] `plasma-rp2040` + `plasma-rp2350` CI targets, board-chip/version stamped (green on `main`)
- [x] On-hardware verify — a Plasma 2350 (no wifi) drives a 66-LED strip correctly

## Technical notes

- Strip-native self-tests (`selftest.hpp` `7x`, plasma-rendered): `70` spectrum,
  `71` decade markers, `72` endpoints (length check without counting). The generic
  matrix patterns mostly degenerate on a 1-tall strip; the `0x` fills and `2x`
  column patterns (ramp / alternating / half) still apply.
- Only WS2812 for the first cut; APA102 (Plasma 2040's `CLK=14`) can follow.
- A serpentine/grid layout mapping is out of scope until a matrix board is on the
  bench — the strip case is linear.

## Out of scope

- The descope/board-identity work (**E9**).
- APA102 support and non-linear (matrix) layouts.
