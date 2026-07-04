# E11 — Panel layout & multi-panel geometry (i75)

**Phase:** 2 · **Depends on:** E9 (S9.4/S9.5 runtime i75 dimensions) · **Issue:** [#51](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/51)

> **Status:** firmware side complete — S11.1–S11.3 landed and verified on hardware
> (i75w, two 128×64 panels as a 1×2 stack rig). The epic stays open only for the
> host-side image fold in pixel-multiverse
> ([#3](https://github.com/elaurijssens/pixel-multiverse/issues/3)), which has the
> finalized geometry contract.

## Goal

Separate **panel size**, **display layout**, and the raw **hub75 chain** so an i75
can drive a grid of identical panels (e.g. two 128×64 stacked into a 128×128, or
four 64×32 into a 128×64) and its **self-tests render correctly across the physical
arrangement** — by calculation, not buffer tricks. The host keeps building the
image, but now reads the geometry parameters instead of assuming a flat panel.

## Why

Today the firmware stores only a flat `width`×`height` and treats the framebuffer
as one panel. That's actually the *electrical chain* geometry: a real 256×64 is
two 128×64 panels chained side-by-side, and the driver's linear framebuffer happens
to match. The moment panels are **stacked** (128×128) or **gridded** (2×2), the
logical display no longer equals the chain — the hub75 driver still only scans
`panel_h ≤ 64` rows, so all panels live in one wide chain (`panel_w · N` × `panel_h`)
and the logical layout must be *mapped* onto it. The host already owns that fold
for streamed images (pixel-multiverse
[#3](https://github.com/elaurijssens/pixel-multiverse/issues/3)); the firmware needs
the same awareness so **self-tests** (which generate their own content) land on the
right physical pixels, and so the host can **read** the arrangement rather than
guess it.

## Proposed shape

Three geometries, all stored, cross-checked at boot:

- **panel** `panel_w × panel_h` — one physical panel (all panels identical).
- **layout** `panels_x × panels_y` — panels across × down.
- **display** `display_w × display_h` — the logical canvas the host renders into;
  must equal `panel_w·panels_x × panel_h·panels_y` (rejected otherwise).
- **chain** — an enum selecting how grid position `(col,row)` maps to the panel's
  sequence in the electrical chain (calibrated by eye with the layout self-test).

Derived, never stored:

- **chain geometry** = `panel_w · panels_x · panels_y` wide × `panel_h` tall. This
  is what the `Hub75` driver and framebuffer are built at. Because
  `chain_w · chain_h = display_w · display_h`, the buffer is unchanged — the cap
  stays **`display_w · display_h ≤ 16384`** (256×64-equivalent), plus `panel_h ≤ 64`
  (hub75 scans `panel_h/2` rows via 5 address lines). Smaller panels simply buy
  larger grids within the same buffer.

Mapping (self-test rendering only — streamed `data`/`zdat` stay a raw blit the host
pre-folds):

```
pcol = lx / panel_w      px = lx % panel_w      // lx,ly in logical display space
prow = ly / panel_h      py = ly % panel_h
seq  = chain_seq(pcol, prow, panels_x, panels_y, chain)   // 0 .. N-1
cx   = seq * panel_w + px                        // flat chain framebuffer
cy   = py
```

`chain_seq` starter set (a single horizontal row keeps `seq = pcol`, so the current
256×64 stays identity — no behaviour change):

| `chain` value   | rows run… | within a row |
|-----------------|-----------|--------------|
| `raster-td`     | top → down    | left → right |
| `serpentine-td` | top → down    | alternating  |
| `raster-bu`     | bottom → up   | left → right |
| `serpentine-bu` | bottom → up   | alternating  |

(The self-test makes the correct value obvious; the list can grow as boards appear.)

## User stories

### S11.1 — Panel/display/layout parameter model + validation ([#52](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/52))
*As a board owner, I want to describe my panels, layout and display as distinct config, validated for consistency.*
- [x] k/v keys `panel_w`,`panel_h`,`panels_x`,`panels_y`,`disp_w`,`disp_h`,`chain` (keys are ≤ 8 bytes)
- [x] Boot-time validation: `display == panel × layout`, `panel_h` even & ≤ 64,
  `disp_w·disp_h ≤ 16384`, `chain` in the known set — else diagnostic + safe fallback
  (verified on hardware: inconsistent `disp_w` → `cfg: bad geometry` + 256×64 fallback)
- [x] Legacy `width`/`height` still work, read as a single 1×1 panel (identity mapping)
- [x] Driver + framebuffer built at the derived chain geometry; `width()/height()` report the display

### S11.2 — Layout-aware self-tests ([#53](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/53))
*As a board owner, I want self-tests that render across the physical arrangement so I can verify size and wiring.*
- [x] `chain_seq` mapping implemented for the starter `chain` set (verified: live
  `raster-td` ↔ `raster-bu` switch swaps top/bottom on the 1×2 rig)
- [x] Dimensions test (42) renders the **logical-display** border + `disp_w×disp_h`, mapped across panels
- [x] New **layout** test (60): each panel shows its `(col,row)` + chain `seq` and a per-panel border, so the arrangement and `chain` value can be confirmed by eye
- [x] Single-row default verified identical to the current 256×64 output
- [x] Geometry (30), corners (31) and grid (50) also render logically for multi-panel;
  panel-level fills/rows/columns stay per-panel (verified on a 1×2 stack rig)

### S11.3 — Host tooling + parameter exposure ([#54](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/54))
*As a host, I want to set the layout from the bench and read the geometry from the board.*
- [x] `multiverse-ctl.sh layout PWxPH COLSxROWS [chain]` — sets panel+layout, computes+stores display, validates, reboots, runs the layout self-test (verified: `layout 128x64 1x2` reproduces the stack in one command)
- [x] `dims WxH` reframed as the 1×1 shortcut (panel = display, layout 1×1)
- [ ] pixel-multiverse ([#3](https://github.com/elaurijssens/pixel-multiverse/issues/3)) reads `panel`/`layout`/`chain` and folds the streamed image to match — **cross-repo**; finalized geometry contract handed to #3

## Technical notes

- All panels share hub75 address + data lines, so a grid is **uniform** (one panel
  size) and always one electrical chain of `panel_h` rows. The physical arrangement
  is decoupled from the chain and expressed entirely by `chain_seq`.
- The mapping is only for firmware-generated content (self-tests). Streamed frames
  remain a raw blit into the flat chain buffer; the host does the fold using the
  same parameters — one source of truth, two consumers.
- Fallback config = single flat 256×64 panel (`panel 256×64`, layout 1×1), matching
  today's default, so a bad/absent config is always renderable.

## Out of scope

- Growing the framebuffer beyond 16384 px (a bigger buffer / per-chip cap is a
  separate change if ever needed).
- Non-identical panels, rotation, and per-panel colour correction.
- The Plasma strip/grid mapping (**E10**) — this epic is i75/hub75 only.
