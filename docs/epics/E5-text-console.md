# E5 — Diagnostic text console

**Phase:** 3 · **Depends on:** E3 · **Unlocks:** better field diagnostics

## Goal

A text buffer with multiline rendering so the firmware can print human-readable
diagnostics on the display — not just the single short string the current
`info()` shows, but wrapped, scrolling, multiline output on the larger panels.

## Why

Today diagnostics are limited to `display::info()` which clears the screen and
draws one line of text (`src/display/*/*.cpp`). On a 128×32 or 256×64 i75 there's
room for real diagnostic output (boot status, config dump, errors). This is
valuable for debugging the runtime-config model from E2/E3 in the field.

## Proposed shape

- A text buffer (rows × cols derived from display dimensions and font metrics).
- A console API: `print`, `println`, `clear`, maybe `printf`-style formatting.
- Word/char wrapping to display width; scroll when full.
- Rendered through PicoGraphics using the descriptor's font (E3).
- A command to push text from the host for on-device display.

## User stories

### S5.1 — Text buffer + renderer ([#25](https://github.com/elaurijssens/gu-multiverse/issues/25))
*As a developer, I want a multiline text buffer rendered to the display so diagnostics aren't one-line-only.*
**Acceptance criteria**
- [ ] Console computes rows/cols from current dimensions + font.
- [ ] `print`/`println`/`clear` work; output wraps at display width.
- [ ] Renders correctly on small (16×16) and large (256×64) displays.

### S5.2 — Scrolling ([#26](https://github.com/elaurijssens/gu-multiverse/issues/26))
*As a user, I want new lines to scroll old ones off so a long log stays readable.*
**Acceptance criteria**
- [ ] When the buffer fills, oldest lines scroll off the top.
- [ ] Scroll behaviour is smooth/legible at the target font sizes.

### S5.3 — Host text command ([#27](https://github.com/elaurijssens/gu-multiverse/issues/27))
*As a host, I want to send text to display for diagnostics.*
**Acceptance criteria**
- [ ] A command (via E1) appends/sets console text from the host.
- [ ] Coexists with image (`data`/`zdat`) commands — switching between graphics and console mode is defined.

### S5.4 — Boot/config diagnostics ([#28](https://github.com/elaurijssens/gu-multiverse/issues/28))
*As a board owner, I want the device to show its config on boot so I can verify setup.*
**Acceptance criteria**
- [ ] On boot (or on demand), the console can show board type, dimensions, WiFi state from the k/v store.
- [ ] Replaces/augments the current `"rdy"` message.

## Technical notes

- Fonts available: bitmap5 (Unicorns), bitmap8 (i75), plus hershey fonts are
  linked. Pick legible defaults per display size.
- Mode interaction with double buffering (E6) needs definition — console likely
  draws to the same graphics surface.

## Out of scope

- Rich text / colour theming beyond what's needed for legibility.
- Interactive input/terminal emulation.
