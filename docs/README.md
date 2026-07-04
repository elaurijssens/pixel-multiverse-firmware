# pixel-multiverse firmware — Design & Roadmap

Firmware for Pimoroni/Pico LED display boards — **Interstate 75 / 75W** (Hub75
matrix) and **Plasma** (WS2812 strip) — driven over USB by the
[pixel-multiverse](https://github.com/elaurijssens/pixel-multiverse) host library.
This directory holds the design vision and the epic/user-story breakdown that
drives development.

> Baseline **v0.1.0**. Reshaped from the earlier `gu-multiverse` fork: the Unicorn
> boards and all audio were removed, and the runtime-unification plan (E3/E4) was
> retired in favour of separate compile-time images (see below).

## Vision

The firmware is built **per board at compile time** via `MULTIVERSE_BOARD`: a
shared, transport-agnostic **core** (command loop + persistent config store) plus
**one display driver per image**. i75 (Hub75) and Plasma (WS2812) are genuinely
different drivers — not near-duplicates — so we keep **separate images** rather
than a runtime-unified one.

Directions:

- **Four images** — `i75-rp2040`, `i75-rp2350` (75W), `plasma-rp2040`,
  `plasma-rp2350w` — each with a definite `board-chip` identity surfaced over USB.
- **Runtime configuration** (display dimensions, WiFi enable, …) via a persistent
  key/value store. The owner configures the board; the firmware does not
  auto-detect hardware. (The board *type* is compile-time; the store holds the
  rest.)
- A **transport-agnostic command core**, so the same commands work over USB today
  and over WiFi later.
- **Double buffering and multi-board synchronisation**, so images flip in lock-step
  across many boards (over multicast for WiFi variants).
- A **diagnostic text console**.

## Principles

- **Incremental.** Epics are phased so each ships something usable.
- **The owner configures the board, not the firmware.** No hardware
  auto-detection. WiFi is compiled into the "W" variants and enabled via the k/v
  store (refused on a non-WiFi build).
- **Transport independence.** Commands are parsed and dispatched the same way
  regardless of whether bytes arrive over USB or WiFi.
- **Shared core, one driver per image.** The command core and config store are
  board-agnostic; each image links exactly one display driver.

## Epics

| ID | Epic | Phase | Depends on | Issue |
|----|------|-------|------------|-------|
| [E1](epics/E1-command-core.md) | Command processing core | 1 (foundation) | — | [#1](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/1) |
| [E2](epics/E2-kv-store.md) | Key/value configuration store | 1 (foundation) | E1 | [#2](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/2) |
| [E9](epics/E9-descope-identity.md) | i75 + Plasma descope & board identity | 2 | E1, E2 | [#43](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/43) |
| [E10](epics/E10-plasma-board.md) | Plasma board (WS2812) | 2 → 3 | E9 | [#47](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/47) |
| [E5](epics/E5-text-console.md) | Diagnostic text console | 3 | E1 | [#5](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/5) |
| [E6](epics/E6-double-buffering.md) | Double buffering & multi-board sync | 3 | E1 | [#6](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/6) |
| [E7](epics/E7-wifi-transport.md) | WiFi transport & multicast sync | 4 | E1, E2, E6 | [#7](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/7) |
| [E8](epics/E8-firmware-update.md) | Firmware update | 3 → 4 | E1, E9 | [#36](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/36) |

**Retired:** [E3](epics/E3-runtime-display.md) (runtime display abstraction) and
[E4](epics/E4-chip-family-builds.md) (chip-family build consolidation) — superseded
by compile-time per-board images once the Unicorns were dropped. See E9.

### Phasing

```
Phase 1 (foundation):   E1 ──┐   E2
                             │    │
Phase 2 (boards):            E9 ◄─┘   →   E10 (Plasma)
Phase 3 (features):     E5,  E6,  E8 (update mode; needs E1, E9)
Phase 4 (networking):   E7  (needs E1, E2, E6)   E8 (embedded update, provisional)
```

## How we work

- Each epic file lists **user stories** with acceptance criteria. Stories are the
  unit of work, and each has a GitHub issue (linked in the tables above).
- Progress is tracked on the
  [pixel-multiverse Roadmap](https://github.com/users/elaurijssens/projects/1)
  GitHub Project board.
- **Acceptance criteria are checkboxes.** Tick each on its issue as it is actually
  met (verified, not assumed). A story is **closed only when every box is checked**.
- Keep this `docs/` tree in sync with reality — when an approach changes, update the
  epic, don't leave it stale.

## Hardware reference

| Board | Driver | W×H | Font | Chip family |
|-------|--------|-----|------|-------------|
| Interstate 75 | `Hub75` | 16×16 … 256×64 (configurable) | bitmap8 | RP2040 |
| Interstate 75 W | `Hub75` | 16×16 … 256×64 (configurable) | bitmap8 | RP2350 |
| Plasma | WS2812 | 1×N (configurable length) | n/a | RP2040 / RP2350W |

No audio: neither board has synth/sample support. See
[`reference/board-pins.md`](reference/board-pins.md) for GPIO maps and the
wrong-driver/switch caveat.
