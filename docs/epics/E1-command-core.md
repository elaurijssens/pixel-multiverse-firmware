# E1 — Command processing core

**Phase:** 1 (foundation) · **Depends on:** — · **Unlocks:** E2, E5, E6, E7

## Goal

Extract the command loop out of `src/main.cpp` into a self-contained, **transport-agnostic** module. A transport supplies bytes; the core frames a command and dispatches it to a registered handler. This is the spine that every later feature (k/v commands, WiFi, sync) plugs into.

## Why

Today the loop is inline in `main()` (`src/main.cpp:113-229`):

- It blocks on the literal prefix `multiverse:`, reads a fixed **4-byte** command, then a chain of `if(command == ...)` branches handles `data`, `zdat`, `note`, `_rst`, `_usb`.
- Byte input is tied directly to USB CDC helpers (`cdc_wait_for`, `cdc_get_bytes`, `tud_task`).
- The data commands assume the compile-time `display::BUFFER_SIZE`.

To add WiFi later and k/v commands now, parsing/dispatch must be decoupled from USB and from any single command's payload shape.

## Proposed shape

- A `Transport` abstraction: "read N bytes with timeout", "peek for prefix", "write N bytes" (write is needed for `get`/diagnostics responses). USB CDC is the first implementation; WiFi is a later one (E7).
- A `CommandRegistry`: maps a 4-byte command id to a handler `fn(Transport&)`. Handlers pull their own payload from the transport.
- A `run()` loop that: pumps the transport, waits for the framing prefix, reads the command id, dispatches. Unknown commands are skipped cleanly.
- `main.cpp` shrinks to: hardware/USB/display init, then `command_core::run()`.

## User stories

### S1.1 — Extract the loop into a module ([#8](https://github.com/elaurijssens/gu-multiverse/issues/8))
*As a developer, I want the command loop in its own translation unit so that `main.cpp` only does setup.*
**Acceptance criteria**
- [x] New module (e.g. `src/command/command_core.{hpp,cpp}`) owns the `multiverse:` framing, command read, and dispatch.
- [x] `main.cpp` calls a single entry point and contains no `if(command == ...)` chains.
- [x] Behaviour is byte-for-byte identical to today for all existing commands.

### S1.2 — Define a transport interface ([#9](https://github.com/elaurijssens/gu-multiverse/issues/9))
*As a developer, I want byte I/O behind an interface so that the same commands can run over USB or WiFi.*
**Acceptance criteria**
- [x] A `Transport` interface with at least: read-with-timeout, prefix-wait, and write.
- [x] A USB CDC implementation wraps the existing `cdc_*` helpers; no behavioural change.
- [x] The core depends only on the interface, not on TinyUSB directly.

### S1.3 — Command registry / dispatch table ([#10](https://github.com/elaurijssens/gu-multiverse/issues/10))
*As a developer, I want commands registered in a table so that adding a command doesn't mean editing a giant if-chain.*
**Acceptance criteria**
- [x] Handlers are registered by 4-byte id.
- [x] Adding a new command is one registration + one handler function.
- [x] Unknown/garbage command ids are skipped without hanging the loop.

### S1.4 — Migrate existing commands ([#11](https://github.com/elaurijssens/gu-multiverse/issues/11))
*As a user, I want all current commands to keep working after the refactor.*
**Acceptance criteria**
- [x] `data`, `zdat`, `note`, `_rst`, `_usb` all behave exactly as before (handler bodies extracted verbatim in S1.3; each verified on i75w hardware).
- [x] The commented-out `wave` command is **dropped** — no host tooling ever sent it and `note` covers synth audio; `display::play_audio()` stays in the API for a future re-add.
- [x] Manual smoke test passes — run via `tools/multiverse-ctl.sh` (`data`/`zdat`/`test`/`note`/`_rst`/`_usb`); the bundled `examples/*.py` target the MicroPython firmware, not this fork (see Technical notes, [#42](https://github.com/elaurijssens/gu-multiverse/issues/42)).

## Technical notes

- Keep command ids 4 bytes for wire compatibility with existing host tooling.
- The framing prefix `multiverse:` stays for USB; revisit framing for WiFi in E7 (datagram boundaries may replace the prefix).
- `_rst`/`_usb` touch `rosc`, watchdog, and `reset_usb_boot` — keep these as handlers but they may live in a small "system" command group.
- Watch the timeout helpers (`init_single_timeout_until`, `check_timeout_fn`) — these changed in SDK 2.0 (see commit `0c997a1`); keep them encapsulated in the USB transport.
- **Host tooling:** the bundled `examples/*.py` + `lib/multiverse` target the
  **MicroPython** Multiverse firmware and its pixel format — they don't drive this
  C++ firmware, which renders through PicoGraphics `PenRGB888` (plain `data`/`zdat`,
  byte order `B,G,R,0`). Host tooling for this firmware lives in `tools/`
  (`multiverse-ctl.sh`, `multiverse-image.py`), which is what the S1.4 smoke test
  used. Growing that into a native client/demo set is tracked by
  [#42](https://github.com/elaurijssens/gu-multiverse/issues/42) (backlog).

## Out of scope

- WiFi transport (E7) — only the interface must accommodate it.
- New commands beyond a clean migration (k/v commands are E2).
