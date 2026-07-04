# pixel-multiverse firmware

C++/Pico SDK firmware for Pimoroni LED display boards. A host streams pixel
buffers over USB serial and the board renders them; the canonical host driver is
the separate [pixel-multiverse](https://github.com/elaurijssens/pixel-multiverse)
Python package.

Supported boards:

- **Interstate 75 / 75W** (Hub75 matrix) — `i75`
- **Plasma** (WS2812 strip) — `plasma` *(in progress, see epic E10)*

## Images: one per board × chip

The firmware is built **per board, at compile time** (`MULTIVERSE_BOARD`), for
both RP2040 and RP2350 chip families. CI
([`.github/workflows/cmake.yml`](.github/workflows/cmake.yml)) builds the full
matrix and, on each GitHub release, attaches a version-stamped image per target:

| Image | Board | Chip | Flash to |
|-------|-------|------|----------|
| `i75-rp2040-multiverse-<version>.uf2`  | Interstate 75  | RP2040 | Interstate 75 |
| `i75-rp2350-multiverse-<version>.uf2`  | Interstate 75W | RP2350 | Interstate 75W |

Plasma images (`plasma-rp2040`, `plasma-rp2350w`) join the matrix when E10 lands.

Pick the image matching your board's chip family, then drop the board into
BOOTSEL/UF2 mode and copy the `.uf2` onto the mounted drive. The board reports
its identity (e.g. `i75-rp2350`) in its USB Product string and on its boot
screen, so you can confirm the right image is running.

## Building locally

CMake + the [Pico SDK](https://github.com/raspberrypi/pico-sdk). Set
`PICO_SDK_PATH` to your checkout; select the board with `MULTIVERSE_BOARD` and
the chip with `PICO_BOARD` (`pico` → RP2040, `pico2` → RP2350):

```bash
# i75, RP2350 (Interstate 75W)
MULTIVERSE_BOARD=i75 cmake -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_BOARD=pico2 \
  -G Ninja -S . -B cmake-build-debug-rp2350
cmake --build cmake-build-debug-rp2350
```

The output is `${board}-multiverse.uf2` in the build tree.

`tools/multiverse-ctl.sh` drives a connected board (reset, self-test, image
send, config, panel dimensions) and has a `flash` subcommand that rebuilds,
archives a version-stamped image under `dist/`, flashes over BOOTSEL, and runs a
self-test to confirm the board came back healthy. See
[`CLAUDE.md`](CLAUDE.md) for the build, wire protocol, and bench workflow.
