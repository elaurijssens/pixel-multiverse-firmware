# E2 — Key/value configuration store

**Phase:** 1 (foundation) · **Depends on:** E1 (for the commands) · **Unlocks:** E3, E4, E7

## Goal

A small **persistent** key/value store in flash that survives reboots and reflashes-of-firmware-only. It holds the board's runtime configuration — board type, display dimensions, WiFi-enabled flag, and "much more later" — and is reachable over the command core via `put`/`get`/`del`.

## Why

The two-image goal requires the firmware to learn *which* board it is and *what
dimensions* to use at runtime, because the board owner — not the firmware —
chooses the hardware. The k/v store is where that choice is recorded. It is the
prerequisite for E3 (runtime display) and E4 (chip-family images), and later
holds the WiFi enable flag (E7).

## Data model

**Decided (S2.1) — encoded in `src/config/kv_format.hpp`:**

- **Key:** 8 bytes, alphanumeric, **space-padded** if shorter.
- **Value:** 64 bytes, **untyped** (raw bytes). Interpretation is the caller's job.
- **Record:** **128 bytes** = key(8) + value(64) + **reserved(56)**. 128 is the
  smallest power of two that fits the 8+64 payload; the reserved bytes leave room
  for future per-record metadata (valid/tombstone flag, CRC, key length) without
  changing the record size or reformatting. Power-of-two records keep offset math
  trivial (`record N` at `RECORD_SIZE*(N+1)`) and pack evenly into the 4 KB erase
  sector (**header + 31 records per sector**).
- **Region header:** occupies slot 0 (first 128 bytes) — `magic "MVKV"` +
  `version(u16)` + `record_size(u16)` + reserved. Detects an
  uninitialised/foreign region and gates future format changes.
- **Slot sentinels (`key[0]`):** `0xFF` = erased/empty (fresh flash), `0x00` =
  tombstone (deleted). Valid keys are alphanumeric, so neither collides; deletion
  writes `0x00` and needs no sector erase (flash writes only clear bits 1→0).
- **Endianness:** header multi-byte fields are little-endian (device native).

## Persistence

- Store in flash, not RAM. On RP2040/RP2350 the SDK exposes flash via
  `hardware/flash.h`; reserve a region at the top of flash (outside the program
  image) using `PICO_FLASH_SIZE_BYTES`.
- Flash is erased per-sector (4 KB) and written per-page (256 B). A naive
  "erase + rewrite whole region" is fine for the first cut given low write
  frequency. Note wear/atomicity as a known limitation; a log-structured or
  double-buffered sector scheme can come later.
- Writes must disable interrupts / pause the other core per SDK guidance, and on
  RP2040 cannot run from the core executing XIP — follow `flash_range_program`
  rules.

## Commands (over E1)

Proposed 4-byte ids (finalise in S2.2):

| Command | Payload in | Response out |
|---------|-----------|--------------|
| `put ` | key(8) + value(64) | ack/err |
| `get ` | key(8) | value(64) or not-found |
| `del ` | key(8) | ack/err |

`get` requires the transport **write** path from E1/S1.2.

## User stories

### S2.1 — Define the record format ([#12](https://github.com/elaurijssens/gu-multiverse/issues/12))
*As a developer, I want a fixed on-flash record layout so reads/writes are simple and forward-compatible.*
**Acceptance criteria**
- [x] Record layout documented (key width, value width, padding, total size, endianness if any). — see Data model above + `src/config/kv_format.hpp`.
- [x] Power-of-two alignment decision made and recorded with rationale. — 128-byte record (8+64+56 reserved).
- [x] A magic/version marker exists at the head of the region so future format changes are detectable. — `kv::Header` (`magic "MVKV"` + version + record_size).

### S2.2 — In-RAM store API ([#13](https://github.com/elaurijssens/gu-multiverse/issues/13))
*As a developer, I want a clean `put/get/del/iterate` API so the rest of the firmware reads config without touching flash directly.*
**Acceptance criteria**
- [ ] Header exposes `put(key, value)`, `get(key) -> value?`, `del(key)`, and iteration.
- [ ] Keys are normalised (space-padded/truncated to 8) consistently in one place.
- [ ] Unit-testable on host where feasible (logic separated from flash I/O).

### S2.3 — Flash persistence ([#14](https://github.com/elaurijssens/gu-multiverse/issues/14))
*As a user, I want my configuration to survive power cycles.*
**Acceptance criteria**
- [ ] Store loads from flash at boot and persists changes.
- [ ] Reserved flash region does not overlap the program image (verified via linker/map).
- [ ] Interrupt/second-core safety for writes implemented per SDK rules.
- [ ] Power-loss during write does not brick the device (at minimum: corrupt store falls back to empty/defaults).

### S2.4 — `put`/`get`/`del` commands ([#15](https://github.com/elaurijssens/gu-multiverse/issues/15))
*As a host, I want to read and write config over the existing transport.*
**Acceptance criteria**
- [ ] The three commands are registered with the E1 core and round-trip correctly.
- [ ] `get` of a missing key returns a clear not-found response.
- [ ] Host-side helper/example demonstrates setting and reading a key.

### S2.5 — Well-known keys catalogue ([#16](https://github.com/elaurijssens/gu-multiverse/issues/16))
*As a developer, I want a documented set of reserved keys so config is consistent across boards.*
**Acceptance criteria**
- [ ] A table of reserved keys is documented (e.g. `board`, `width`, `height`, `wifi`, …) with value encodings.
- [ ] Defaults are defined for when a key is absent.

## Out of scope

- Wear-levelling / log-structured storage (note as future work).
- Encryption/auth of values.
- Typed values — values stay raw bytes; typing is the caller's concern.
