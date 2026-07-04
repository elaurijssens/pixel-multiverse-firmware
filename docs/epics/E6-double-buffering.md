# E6 — Double buffering & multi-board sync

**Phase:** 3 · **Depends on:** E1 (command core), E9/E11 (runtime dimensions) · **Unlocks:** E7 (multicast sync)

## Goal

Add an off-screen ("back") buffer and a **flip** command so that image data can be
loaded without showing it, then made visible on command. This enables **synchronised
display across multiple boards** with short commands.

## Why

Today `data`/`zdat` read straight into the single visible framebuffer and call
`display::update()` immediately (`command_core.cpp` `handle_data`/`handle_zdat`).
To show the same frame on many boards at the same instant, each board must
pre-load its frame and flip on a shared signal. Double buffering is the
prerequisite; multicast distribution of the flip is E7.

## Proposed shape

- Two framebuffers per board, **front** (visible) and **back** (write target),
  **heap-allocated at the configured dimensions in `init()`** (Option A) — the
  static max buffer is dropped, so a smaller config costs less RAM.
- `data`/`zdat` read into the **back** buffer (via a `display::back()` accessor).
- A **`flip`** command swaps front↔back and renders the new front (cheap pointer
  swap + one driver push).
- Mode lives in the command layer: **immediate by default** (`data`/`zdat`
  auto-flip = today's behaviour), with **`hold`** / **`live`** to toggle deferred
  mode for the load-then-flip sync path. Diagnostics (`info`/`selftest`) always
  present immediately.
- **Graceful RAM fallback:** if the back-buffer allocation fails (RP2040 near max
  dimensions — see notes), the board runs **single-buffered** (writes go to the
  visible buffer, `flip` renders in place) with a diagnostic. So double buffering
  auto-enables where it fits (always on RP2350; up to ~128×64 on RP2040) without
  hard-coded per-chip caps.

## User stories

### S6.1 — Back buffer + flip ([#29](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/29))
*As the firmware, I want to write to a hidden buffer and flip it on command.*
**Acceptance criteria**
- [x] `data`/`zdat` read into the back buffer (`display::back()`), not the visible one.
- [x] A `flip` command swaps front↔back and renders the new front atomically (verified: held
  frame stays hidden, flip snaps to it).
- [x] Works on both drivers: i75 (hub75 `panel->update(front)`) and plasma (walk front → WS2812).

### S6.2 — Backwards-compatible immediate mode ([#30](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/30))
*As an existing host, I want current behaviour to keep working without sending a flip.*
**Acceptance criteria**
- [x] Default **immediate** ("live") mode preserves load-then-show; `hold`/`live` toggle it.
- [x] Existing hosts unchanged — `data`/`zdat` behave exactly as before unless `hold` is sent.

### S6.3 — Memory budget per family ([#31](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/31))
*As a developer, I want double buffering to fit within each chip's RAM.*
**Acceptance criteria**
- [x] Allocation strategy decided + recorded: **heap, runtime-sized** (Option A).
- [x] Back-buffer allocation failure falls back to single-buffered + a boot diagnostic
  (`1buf …`, i75) — defensive; both buffers actually fit at init for all supported dims.
- [x] Per-family behaviour documented (below).

## Technical notes

- The flip is a cheap pointer swap plus one driver push of the new front.
- **Measured (i75).** Moving both framebuffers to the heap dropped static bss from
  ~89 KB to **~25 KB**. At init even a 256×64 board fits on RP2040 (264 KB):
  25 (bss) + 64 + 64 (two buffers) + 64 (hub75's own back_buffer) ≈ **217 KB** — so
  the single-buffer fallback is defensive, not normally taken.
- **Per-family limits.** RP2350 (520 KB): full 256×64 double-buffered, `data` and
  `zdat`, ample headroom. RP2040 (264 KB): double-buffered at all sizes, `data`
  always fine; **`zdat` at 256×64 is marginal** — decode adds zlib's ~44 KB (+ the
  compressed blob) on top of ~217 KB, right at the edge. If it can't allocate, the
  frame is **dropped gracefully** (the `zdat` handler already null-checks). Use
  **≤128×64** (comfortable) or uncompressed **`data`** for reliable RP2040 use.
- Mode state (immediate vs deferred) lives in `command_core`; the display layer
  just exposes `back()` and `flip()`. Diagnostics present immediately regardless.
- This sets up the contract E7 relies on: "load now, flip on shared signal."

## Out of scope

- Network distribution of frames / the shared signal itself (E7).
- More than two buffers (triple buffering) — note if it becomes necessary.
