# Multiverse Firmware — Design & Roadmap

Firmware for Pimoroni/Pico-based LED display boards (Galactic, Cosmic, Stellar
Unicorn, Interstate 75/75W, Plasma). This directory holds the design vision and
the epic/user-story breakdown that drives development.

## Vision

Today the firmware is built **per board** via the compile-time `MULTIVERSE_BOARD`
switch. Each board has a near-identical copy of the display code under
`src/display/<board>/`, differing only in driver class, dimensions, font, and
audio support. The command loop lives inline in `src/main.cpp` and is hard-wired
to a compile-time buffer size.

We are moving to:

- **Two build images**, one per chip family: **RP2040** and **RP2350** — instead
  of one image per board.
- **Runtime board configuration** via a persistent key/value store. The board
  owner selects chip + WiFi/non-WiFi hardware and writes the board type and
  dimensions into the store; the firmware does not auto-detect hardware.
- A **transport-agnostic command core**, so the same commands work over USB today
  and over WiFi later.
- **Double buffering and multi-board synchronisation**, so images can be flipped
  in lock-step across many boards (over multicast for WiFi variants).
- A **diagnostic text console** with multiline output on larger displays.

## Principles

- **Incremental.** We do not implement everything at once. Epics are phased so
  each one ships something usable.
- **The owner configures the board, not the firmware.** No hardware
  auto-detection. WiFi may be compiled in but disabled via the k/v store.
- **One code path per concept.** Collapse the four duplicated display
  implementations into a single descriptor-driven one.
- **Transport independence.** Commands are parsed and dispatched the same way
  regardless of whether bytes arrive over USB or WiFi.

## Epics

| ID | Epic | Phase | Depends on |
|----|------|-------|------------|
| [E1](epics/E1-command-core.md) | Command processing core | 1 (foundation) | — |
| [E2](epics/E2-kv-store.md) | Key/value configuration store | 1 (foundation) | E1 (for commands) |
| [E3](epics/E3-runtime-display.md) | Runtime display abstraction | 2 | E2 |
| [E4](epics/E4-chip-family-builds.md) | Chip-family build consolidation | 2 | E3 |
| [E5](epics/E5-text-console.md) | Diagnostic text console | 3 | E3 |
| [E6](epics/E6-double-buffering.md) | Double buffering & multi-board sync | 3 | E3 |
| [E7](epics/E7-wifi-transport.md) | WiFi transport & multicast sync | 4 | E1, E2, E6 |

### Phasing

```
Phase 1 (foundation):   E1 ──┐        E2
                             │         │
Phase 2 (two images):        E3 ◄──────┘   →   E4
Phase 3 (features):     E5,  E6
Phase 4 (networking):   E7  (needs E1, E2, E6)
```

## How we work

- Each epic file lists **user stories** with acceptance criteria. Stories are the
  unit of work.
- Progress is tracked on a GitHub Project board (to be set up; see the epic
  files as the source of truth until then).
- Keep this `docs/` tree in sync with reality — when an approach changes, update
  the epic, don't leave it stale.

## Hardware reference

| Board | Driver class | W×H | Font | Audio | Chip family |
|-------|--------------|-----|------|-------|-------------|
| Cosmic Unicorn | `CosmicUnicorn` | 32×32 | bitmap5 | yes | RP2040 |
| Galactic Unicorn | `GalacticUnicorn` | 53×11 | bitmap5 | yes | RP2040 |
| Stellar Unicorn | `StellarUnicorn` | 16×16 | bitmap5 | yes | RP2350 |
| Interstate 75 | `Hub75` | 16×16 … 256×64 (configurable) | bitmap8 | no | RP2040 / RP2350 (75W) |
| Plasma | (plasma) | 1×N (configurable length) | n/a | tbd | RP2040 / RP2350 |
