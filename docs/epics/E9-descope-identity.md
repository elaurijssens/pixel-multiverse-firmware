# E9 — i75 + Plasma descope & board identity

**Phase:** 2 · **Depends on:** E1, E2 · **Replaces:** E3, E4 · **Issue:** [#43](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/43)

## Goal

Commit to **compile-time per-board images** for i75 + Plasma across RP2040/RP2350,
**remove the Unicorns and all audio**, and give each image a definite `board-chip`
identity surfaced over USB.

## Why

The unification epics (E3/E4) existed to de-duplicate four near-identical Unicorn
boards. With the Unicorns dropped, only **i75 (Hub75)** and **Plasma (WS2812)**
remain — genuinely different drivers, not copy-paste. Separate compile-time images
are simpler and correct, and they design out a class of Unicorn-specific problems
(the refresh-vs-USB read contention, and the worst button-shorting pin overlaps).
What's left is a cleanup plus a clear per-image identity.

## User stories

### S9.1 — Remove the Unicorn boards ([#44](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/44))
*As a maintainer, I want Galactic/Cosmic/Stellar removed so the tree only covers i75 + Plasma.*
- [ ] Delete `src/display/{galactic,cosmic,stellar}/` + their `.cmake`
- [ ] `MULTIVERSE_BOARD` accepts only `i75`/`plasma`; default updated
- [ ] `board-pins.md` + docs reflect i75 + Plasma only

### S9.2 — Remove audio + the `note` command ([#45](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/45))
*As a maintainer, I want audio gone since only the Unicorns had it.*
- [ ] Remove `play_note`/`play_audio` from the `display::` API and all boards
- [ ] Remove the `note` command handler (`command_core`)
- [ ] Retire the now-superseded `board_config` runtime descriptor (board is compile-time)

### S9.3 — `board-chip` identity + USB Product string ([#46](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/46))
*As a host, I want to identify the exact board (`i75-rp2040`/`i75-rp2350`/`plasma-rp2040`/`plasma-rp2350w`) straight from USB.*
- [ ] Build identity `board-chip` in `MULTIVERSE_BOARD`/`version.hpp` (board + `PICO_PLATFORM`)
- [ ] USB **Product string** embeds the board id; VID/PID unchanged (`0xCAFE`/`0x0200`) for tooling compatibility
- [ ] `multiverse-ctl.sh` can label a detected port by board from USB metadata

### S9.4 — Runtime framebuffer sizing (i75 W×H from k/v) ([#20](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/20)) · *was S3.4*
*As the firmware, I want the framebuffer sized for the configured dimensions.*
- [ ] Framebuffer sized for the configured i75 W×H (from the k/v store), not a compile-time constant
- [ ] `data`/`zdat` use the runtime size; largest supported mode fits the chip's RAM

### S9.5 — Configurable i75 dimensions from k/v ([#21](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/21)) · *was S3.5*
*As a board owner, I want to set my i75 resolution via the k/v store.*
- [ ] i75 width/height read from k/v with range validation (width 16…256, height 16…64 —
  hub75 scans ≤ 64 rows; stacked panels are folded host-side, not here)
- [ ] Out-of-range values rejected with a diagnostic + fallback

### S9.6 — CI builds all per-board images ([#24](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/24)) · *was S4.3*
*As a maintainer, I want CI to produce every board image.*
- [x] CI builds `i75-rp2040` + `i75-rp2350`, board-chip/version stamped (green on `main`:
  both jobs pass, artifacts `i75-<chip>-multiverse-<version>.uf2`)
- [ ] `plasma-rp2040` + `plasma-rp2350w` — matrix rows are wired but commented, **gated on
  E10** (no `src/display/plasma` yet); uncomment when E10 lands its display module
- [x] Artifacts published per release (verified: `v0.1.0-rc.1` release attached both
  `i75-rp2040`/`i75-rp2350` version-stamped UF2s — needed a `permissions: contents: write` fix)

Implemented in [`.github/workflows/cmake.yml`](../../.github/workflows/cmake.yml): a
board × chip matrix building each image the way the bench does (`PICO_BOARD` `pico` →
rp2040, `pico2` → rp2350), stamping `${board}-${chip}-multiverse-${version}.uf2` from
`git describe`, uploading it as a CI artifact, and attaching it to GitHub releases.

## Technical notes

- `board-chip` = `MULTIVERSE_BOARD` + `PICO_PLATFORM` (`rp2040`/`rp2350`) → a string in
  `version.hpp`, embedded in the USB Product string. The **`multiverse:` wire prefix and
  `MULTIVERSE_BOARD` name stay** (host-protocol compatibility — see CLAUDE.md).
- Removing audio collapses the `display::` API to `init`/`update`/`info`/`selftest` +
  `buffer`, and drops the `note` command from the registry.

## Out of scope

- The Plasma WS2812 driver itself (**E10**).
