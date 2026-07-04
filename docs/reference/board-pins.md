# Board pin maps

GPIO assignments for the supported boards — **Interstate 75 / 75W** (Hub75) and
**Plasma** (WS2812/APA102). Since each firmware image links **one** display driver at
compile time, you can't select the wrong driver, so the historic "wrong-driver"
hazard is designed out. What remains is a small **i75 ↔ Plasma** pin overlap and a
switch caveat.

Source: the Pimoroni C++ drivers this firmware builds against — `drivers/hub75`,
`drivers/plasma` (`ws2812`/`apa102`), `libraries/plasma{_stick,2040}`.

## ⚠️ Switch caveat

The firmware never reads the board buttons. The only way to get in trouble is to
flash the *wrong board's* image onto hardware (e.g. an i75 image on a Plasma): a
driver output can then land on a switch-to-ground pin, and pressing that switch
briefly shorts a driven GPIO. Flash the matching `board-chip` image and it's a
non-issue.

## Chip family

| Board | Chip | Driver |
|---|---|---|
| Interstate 75 | RP2040 | Hub75 |
| Interstate 75 W | RP2350 | Hub75 |
| Plasma 2040 / Stick | RP2040 | WS2812 / APA102 |
| Plasma 2350 W | RP2350 | WS2812 / APA102 |

## Pin maps

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

## i75 ↔ Plasma overlap

Shared GPIOs (only relevant under a wrong-image flash): **14** (i75 `SW_A` button vs
Plasma APA102 `CLK`), **15** (Plasma `DATA`; unused by Hub75), **12/13** (Hub75
`STB`/`OE` vs Plasma 2040 buttons), **16–18** (both drive the onboard LED), **23**
(both `USER_SW`). Plasma-driven images only wiggle 1–2 pins, so they're the safest
mismatch; a Hub75 image on a Plasma drives the button pins — hence the caveat above.

> **Historical (pre-v0.1.0):** earlier versions also supported the Unicorn boards,
> whose shift-register/audio pins (0–8, 13–22) overlapped these much more heavily
> (a Hub75 image drove six Unicorn button pins). Retired with the Unicorns.
