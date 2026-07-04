# E12 — Factory reset & config recovery

**Phase:** 3 · **Depends on:** E2 (k/v store + flash region) · **Issue:** [#56](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/56)

## Goal

Give a board owner a reliable way to return a board to **factory defaults** —
especially when the **stored config is what prevents it from booting**. Reflashing
firmware doesn't help there: the config lives in its own flash region and survives
a new image, so a bad value (a geometry that wedges init, a corrupt record) keeps
biting until the config is cleared.

## Why

Config is now rich (i75 panel/layout/chain, plasma length/order, and more to come),
read at boot and used to build the display before USB is fully up. A value that
crashes or hangs `init()` would leave the board unusable and **unable to accept a
USB factory-reset command** — so the recovery path can't depend on USB alone. A
physical, before-config escape hatch is the safety net; a USB command is the
convenient everyday path.

## Proposed shape

- **USB command** (`_fac` or similar, a system handler alongside `_rst`/`_usb`):
  erase the config flash region / empty the live store, show a clear state, reboot.
- **Button-held-at-boot:** very early in `main()` — **before `kv::config_boot()` reads
  the config** — sample a board button; if held, erase the config region so the
  bad data never gets applied. This is the true recovery mechanism.
- Both land the board at the same place: empty store → firmware boots on built-in
  defaults (i75 256×64, plasma length 64, etc.).

## User stories

### S12.1 — Factory-reset command over USB ([#57](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/57))
*As a board owner, I want a command that clears the persistent config back to factory defaults, so I can recover from bad config without a full reflash.*
- [x] The `_fac` system command clears the k/v store (erases the config flash region)
- [x] Display shows "factory reset", then reboots into defaults
- [x] Exposed via `multiverse-ctl.sh factory`
- [x] Verified on the Plasma 2350: store goes empty, board boots at default length 64 (via `diag`)

### S12.2 — Clear config on button-held-at-boot (recovery) ([#58](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/58))
*As a board owner, I want to force a factory reset by holding a button during early startup, so a config that prevents boot — or a board that won't accept USB — can still be recovered.*
- [x] `src/recovery` samples the reset button **before `kv::config_boot()`** in `main()`
- [x] Held (active-low, ~300 ms) → erase the config region before it's read, so a bad config can't re-brick
- [x] Per-board pin: plasma **SW_A (GPIO 12)**; i75 **SW_A (GPIO 14)** identified. Verified on the
  Plasma 2350: holding SW_A across a reboot wipes config; a normal reboot preserves it.
- [ ] Enable + hardware-verify the i75 button (GPIO 14) — deferred (avoid a wrong-pin spurious wipe;
  the i75w is mid soak-test)
- [ ] Explicit "reset happened" indication after boot (currently just boots to defaults)

## Technical notes

- The erase must run **before `kv::config_boot()`** in `main()` for S12.2 to help a
  config that wedges init — that's the whole point.
- Reuse E2's flash-region knowledge; erasing needs `save_and_disable_interrupts()`
  (single core), like the other flash writes.
- Button pins are board-specific (build-time), like the WS2812 data pin; keep them
  in the per-board display/config layer.

## Out of scope

- Selective reset (clearing individual keys) — that's just `del`.
- Backing up / restoring config off-board.
