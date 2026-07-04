# Board pin maps & wrong-driver safety

> **v0.1.0 reshape:** the supported boards are now **i75 + Plasma only** (Unicorns
> dropped), and each image links **one** driver at compile time — so the wrong-driver
> case is largely designed out. The Unicorn tables/rows below are **historical**
> (kept for reference); the live concern is the small **i75 ↔ Plasma** pin overlap
> and the switch caveat.

GPIO assignments per board, and what happens if the firmware runs the **wrong**
display driver for the attached hardware.

Source: the Pimoroni C++ drivers this firmware builds against — `libraries/{galactic,
cosmic,stellar}_unicorn/*.hpp`, `drivers/hub75/hub75.hpp`, `drivers/plasma/ws2812.hpp`,
`libraries/plasma{_stick,2040}/*.hpp`.

## ⚠️ DO NOT OPERATE THE PHYSICAL SWITCHES / BUTTONS

On unified firmware (E4), or any time the configured `board` might not match the
attached hardware, **do not press the board's buttons/switches.** Each board wires
its switches to GPIOs that *another* board's driver uses as **push-pull outputs**.
A switch shorts its pin to ground when pressed; if a mismatched driver is holding
that pin HIGH, pressing the switch shorts a driven output to ground and stresses the
GPIO pad. Worst case: the **Hub75 (i75) driver drives GPIO 0–13**, which are **six of
the Unicorn's button pins** (A/B/C/D/Vol±). The firmware never reads the buttons, so
you lose nothing by leaving them alone.

## Chip family

| Board | Chip | Driver |
|---|---|---|
| Galactic Unicorn | RP2040 | shift-register column scan |
| Cosmic Unicorn | RP2040 | shift-register column scan |
| Stellar Unicorn | RP2350 | shift-register column scan |
| Interstate 75 | RP2040 | Hub75 |
| Interstate 75 W | RP2350 | Hub75 |
| Plasma 2040 / Stick | RP2040 | WS2812 / APA102 |
| Plasma 2350 W | RP2350 | WS2812 / APA102 |

## Pin maps

### Unicorns — Galactic / Cosmic / Stellar (identical pinout)

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| COLUMN_CLOCK | 13 | | I2S_DATA | 9 |
| COLUMN_DATA | 14 | | I2S_BCLK | 10 |
| COLUMN_LATCH | 15 | | I2S_LRCLK | 11 |
| COLUMN_BLANK | 16 | | MUTE | 22 |
| ROW_BIT_0..3 | 17–20 | | LIGHT_SENSOR | 28 (ADC) |
| **SWITCH_A/B/C/D** | **0 / 1 / 3 / 6** | | **VOL_UP/DOWN** | **7 / 8** |
| **SLEEP** | **27** | | **BRIGHT_UP/DOWN** | **21 / 26** |

Boards differ only in `ROW_COUNT` (Galactic 11, Cosmic/Stellar 16) and panel geometry.

### Interstate 75 / 75 W — Hub75

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| RGB R0 G0 B0 | 0 1 2 | | CLK | 11 |
| RGB R1 G1 B1 | 3 4 5 | | STB (latch) | 12 |
| Row A–E | 6 7 8 9 10 | | OE | 13 |
| **SW_A** | **14** | | **SW_USER** | **23** |
| LED R/G/B | 16 / 17 / 18 | | | |

### Plasma — Stick / 2040 / 2350 W

| Signal | GPIO |
|---|---|
| DATA (WS2812 / APA102) | 15 |
| CLK (APA102 only, Plasma 2040) | 14 |
| Plasma 2040 only: **BUTTON_A/B** | **12 / 13** |
| Plasma 2040 only: USER_SW / LED R,G,B / CURRENT_SENSE | 23 / 16,17,18 / 29 (ADC) |

## Wrong-driver collision analysis

A mismatched driver drives its own pins with its own protocol. Output-vs-output
overlaps are harmless (garbage/flicker). The hazard is a driver **output** landing on
another board's **switch-to-ground input**:

| Active driver → attached board | Driven onto switch pins | Risk |
|---|---|---|
| **Hub75 → Unicorn** | 0,1,3,6,7,8 = SWITCH A/B/C/D + Vol± | ⚠️ six buttons short-to-GND if pressed |
| **Hub75 → Plasma 2040** | 12,13 = BUTTON_A/B | ⚠️ two buttons |
| **Unicorn → i75** | 14 = SW_A | ⚠️ one button |
| **Unicorn → Plasma 2040** | 13 = BUTTON_B | ⚠️ one button |
| **Plasma (WS2812) → anything** | drives only DAT(15) | ✓ harmless (1 pin) |

No instant damage — RP2040/RP2350 pads current-limit at their drive strength — but a
held short is not something to allow in normal operation. Hence the switch warning
above, and the driver-selection rules below.

## Driver-selection safety (E3 / E4)

1. **Per-image driver allow-list (S3.3):** the descriptor may only select a board
   whose driver is *linked into the current image*. Cross-family (RP2040 ↔ RP2350)
   selection is therefore impossible; within a family, an out-of-set `board` value
   fails safe.
2. **A family image has no safe default driver.** Boards in one family use
   *conflicting* pins, so the firmware cannot guess which hardware it is on. When
   `board` is unset or invalid in a family image, the safe behaviour is **bring up no
   display driver** — leave the pins high-impedance (inputs), render nothing, and wait
   for the owner to set `board` over USB (the config commands work without a display).
   This is E2's principle in action: *the owner configures the board; the firmware does
   not auto-detect.*
3. **Per-board images (today)** link exactly one driver, so the descriptor's fallback
   `= multiverse::BOARD` is always the attached hardware and safe to bring up.
