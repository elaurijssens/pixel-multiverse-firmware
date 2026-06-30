# E6 — Double buffering & multi-board sync

**Phase:** 3 · **Depends on:** E3 · **Unlocks:** E7 (multicast sync)

## Goal

Add an off-screen ("back") buffer and a **sync/flip** command so that image data
can be loaded without showing it, then made visible on command. This enables
**synchronised display across multiple boards** with short commands.

## Why

Right now `data`/`zdat` write straight into the single visible buffer and call
`display::update()` immediately (`src/main.cpp:126-185`). To show the same frame
on many boards at the same instant, each board must pre-load its frame and flip
on a shared signal. Double buffering is the prerequisite; multicast distribution
of the flip is E7.

## Proposed shape

- Two buffers: **front** (visible) and **back** (being written). `data`/`zdat`
  write to the back buffer.
- A `sync`/flip command swaps back→front and triggers `update()`.
- Optionally a "load + auto-flip" path preserves today's immediate-update
  behaviour for single-board use (backwards compatible).
- Memory: two buffers double the framebuffer footprint — feeds back into the
  E3/S3.4 sizing decision per chip family.

## User stories

### S6.1 — Back buffer + flip ([#29](https://github.com/elaurijssens/gu-multiverse/issues/29))
*As the firmware, I want to write to a hidden buffer and flip it on command.*
**Acceptance criteria**
- [ ] `data`/`zdat` can target the back buffer.
- [ ] A `sync`/flip command swaps and updates the display atomically.
- [ ] No tearing/partial-frame on flip.

### S6.2 — Backwards-compatible immediate mode ([#30](https://github.com/elaurijssens/gu-multiverse/issues/30))
*As an existing host, I want current behaviour to keep working without sending a flip.*
**Acceptance criteria**
- [ ] A mode (default or explicit) preserves load-then-show-immediately.
- [ ] Existing examples work unchanged, or with a documented one-line change.

### S6.3 — Memory budget per family ([#31](https://github.com/elaurijssens/gu-multiverse/issues/31))
*As a developer, I want double buffering to fit within each chip's RAM.*
**Acceptance criteria**
- [ ] Two buffers at the max supported dimensions fit on RP2040 and RP2350 respectively (or limits are documented per family).
- [ ] Allocation strategy (static max vs dynamic) decided and recorded.

## Technical notes

- The flip should be cheap (pointer swap), with `update()` pushing the new front
  buffer to the driver.
- Interaction with the text console (E5) and audio is independent (audio is not
  buffered here).
- This sets up the contract E7 relies on: "load now, flip on shared signal."

## Out of scope

- Network distribution of frames / the shared signal itself (E7).
- More than two buffers (triple buffering) — note if it becomes necessary.
