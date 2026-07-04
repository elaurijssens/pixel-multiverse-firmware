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
- **Record:** **128 bytes** = key(8) + value(64) + **seq(4)** + **key_len(1)** +
  **value_len(1)** + **flags(1)** + **reserved(45)** + **crc32(4)**. 128 is the
  smallest power of two that fits the 8+64 payload with room for metadata.
  `key_len`/`value_len` give the significant byte counts (padding vs content for
  keys, used length for values); `seq` is the write sequence for the log-structured
  store; `flags` carries record bits (`FLAG_DELETED` = tombstone). New fields can be
  carved from `reserved` later **without moving** the earlier fields or the trailing
  CRC. Power-of-two records keep offset math trivial and pack evenly into the 4 KB
  erase sector.
- **Integrity:** the last 4 bytes of every slot are a **CRC-32** (IEEE 802.3,
  zlib-compatible — zlib's `crc32()` is already linked) over the preceding **124
  bytes** — the whole slot except the CRC word — so key, value, the length fields,
  and any future metadata are all covered. A record is valid only if `key[0]` is
  alphanumeric and the CRC matches, so a write interrupted by power loss fails its
  CRC and is ignored rather than returning garbage.
- **Region header:** occupies slot 0 (first 128 bytes) — `magic "MVKV"` +
  `version(u16)` + `record_size(u16)` + reserved + trailing `crc32(u32)`. Detects
  an uninitialised/foreign region and gates future format changes.
- **Slot sentinels (`key[0]`):** `0xFF` = erased/empty (fresh flash — an append
  target). Valid keys are alphanumeric, so this never collides. Deletion appends a
  tombstone record (a higher-`seq` record for the key marked deleted) rather than
  erasing in place; the exact delete marker is finalised in S2.3.
- **Endianness:** header multi-byte fields are little-endian (device native).

## Persistence

- Store in flash, not RAM. On RP2040/RP2350 the SDK exposes flash via
  `hardware/flash.h`; reserve a region at the top of flash (outside the program
  image) using `PICO_FLASH_SIZE_BYTES`.
- Flash is erased per-sector (4 KB) and written per-page (256 B). The store is
  **log-structured / append-only** for wear-levelling: `put` writes a fresh record
  into the next erased slot with `seq = max(seq)+1` instead of rewriting in place,
  and deletion appends a tombstone. On load, the store scans all slots and keeps
  the **highest-`seq` valid record per key**. This spreads writes across slots
  (and, as the region grows, across sectors) and is naturally power-loss safe: a
  half-written record fails its CRC and the previous highest-`seq` record remains
  authoritative.
- **Compaction:** when the region fills, rewrite the live set (one record per key)
  into a freshly erased region and continue. Sector erase is thus rare, not
  per-write. (First cut may use a single sector; multi-sector rotation can follow.)
- Writes must disable interrupts / pause the other core per SDK guidance, and on
  RP2040 cannot run from the core executing XIP — follow `flash_range_program`
  rules.

## Commands (over E1)

Finalised in S2.4 (`src/config/kv_commands.cpp`). 4-byte ids with a trailing
space; **length-prefixed** payloads (so keys/values are variable and `value_len`
is conveyed); all responses are **status-first**.

| Command | Payload in | Response out |
|---------|-----------|--------------|
| `put ` | `klen(1)` `key[klen]` `vlen(1)` `value[vlen]` | `status(1)` — 1 ok / 0 fail |
| `get ` | `klen(1)` `key[klen]` | `1, vlen(1), value[vlen]` (found) or `0` (not found) |
| `del ` | `klen(1)` `key[klen]` | `status(1)` — 1 deleted / 0 absent |

`klen` is 1..8, `vlen` 0..64; a malformed frame answers status 0 and the loop
re-syncs. `get` uses the transport **write** path from E1/S1.2. Host helper:
`tools/multiverse-config.py` (driven by `multiverse-ctl.sh set|get|del`).

## Well-known keys (S2.5)

Values stay raw bytes (E2 keeps them untyped), but the **reserved** keys below use
a documented **ASCII** encoding so they are settable from the host CLI
(`multiverse-ctl.sh set width 256`) and human-readable — numbers as ASCII decimal,
booleans as `"0"`/`"1"`, strings as raw ASCII (`value_len` bytes, not
NUL-terminated). Reserved keys are lowercase; treat any other key as opaque
app/user data.

| Key | Meaning | Encoding | Default when absent | Consumer |
|-----|---------|----------|---------------------|----------|
| `board` | board model | ASCII (`galactic`, `cosmic`, `stellar`, `i75`, `i75w`, `plasma`) | compile-time `MULTIVERSE_BOARD` | E3/E4 |
| `chip` | chip family | ASCII (`rp2040` \| `rp2350`) | the built image's target | E4/E8 |
| `width` | panel width, px | ASCII decimal | compile-time `display::WIDTH` | E3 |
| `height` | panel height, px | ASCII decimal | compile-time `display::HEIGHT` | E3 |
| `rotate` | rotation, degrees | ASCII decimal (`0` \| `90` \| `180` \| `270`) | `0` | E3 |
| `bright` | brightness | ASCII decimal `0`–`255` | board default | E3 |
| `wifi` | WiFi enabled | ASCII bool (`0` \| `1`) | `0` (disabled) | E7 |
| `name` | friendly/host name | ASCII string (≤ `VALUE_LEN`) | unset (use serial/unique id) | E7 |

**Defaults rule:** the firmware **never auto-detects hardware** (see Principles) —
an absent key means "use the compile-time / documented default" above; the owner
overrides by writing the key. Consumers (E9/E10/E7) read via `kv::config().get()`,
falling back to the default when `get` returns not-found.

**Note (v0.1.0 reshape):** board *type* is now **compile-time** (E3/E4 retired), so
the `board` key is **vestigial** — a record for host tooling, not a runtime selector.
The store still drives `width`/`height` (i75, E9), Plasma `length` (E10), and `wifi`
(E7).

## User stories

### S2.1 — Define the record format ([#12](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/12))
*As a developer, I want a fixed on-flash record layout so reads/writes are simple and forward-compatible.*
**Acceptance criteria**
- [x] Record layout documented (key width, value width, padding, total size, endianness if any). — see Data model above + `src/config/kv_format.hpp`.
- [x] Power-of-two alignment decision made and recorded with rationale. — 128-byte record (key 8 + value 64 + seq 4 + key_len/value_len + reserved 46 + crc 4).
- [x] A magic/version marker exists at the head of the region so future format changes are detectable. — `kv::Header` (`magic "MVKV"` + version + record_size).

### S2.2 — In-RAM store API ([#13](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/13))
*As a developer, I want a clean `put/get/del/iterate` API so the rest of the firmware reads config without touching flash directly.*
**Acceptance criteria**
- [x] Header exposes `put(key, value)`, `get(key) -> value?`, `del(key)`, and iteration. — `kv::Store` in `src/config/kv_store.hpp`.
- [x] Keys are normalised (space-padded/truncated to 8) consistently in one place. — `Store::normalize_key`, used by put/get/del.
- [x] Unit-testable on host where feasible (logic separated from flash I/O). — no SDK/flash deps; `test/kv_store_test.cpp` passes (`seq`/`crc` left to S2.3).

### S2.3 — Flash persistence ([#14](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/14))
*As a user, I want my configuration to survive power cycles.*
**Acceptance criteria**
- [x] Store loads from flash at boot (highest-`seq` valid record per key) and persists changes. — `kv::Log::load`/`put`/`del` (`src/config/kv_log.cpp`); host test + on-device boot/reboot.
- [x] Writes are **log-structured / append-only** with `seq`; sector erase happens only on compaction, not per write (wear-levelling). — one record per 256 B page; `compact()` only when the log fills.
- [x] Reserved flash region does not overlap the program image (verified via linker/map). — 16 KB at top of flash; image ends 0x28530, region 0x3FC000 (≈3.9 MB gap); `__flash_binary_end` runtime guard.
- [x] Interrupt/second-core safety for writes implemented per SDK rules. — single-core; `save_and_disable_interrupts()`/`restore_interrupts()` around erase/program (`kv_flash.cpp`).
- [x] Power-loss during write does not brick the device. — per-record CRC-32: a torn append fails its CRC and the prior record stays authoritative (host test simulates a truncated page); a corrupt/foreign region → format → empty/defaults. **Known limitation:** a power loss *mid-compaction* can lose config → boots empty (defaults, not bricked); a two-region ping-pong compaction is the future hardening.

### S2.4 — `put`/`get`/`del` commands ([#15](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/15))
*As a host, I want to read and write config over the existing transport.*
**Acceptance criteria**
- [x] The three commands are registered with the E1 core and round-trip correctly. — `kv::register_commands()` (`src/config/kv_commands.cpp`); set→get verified on i75w, and set→reboot→get confirms flash persistence end-to-end.
- [x] `get` of a missing key returns a clear not-found response. — status byte `0` → helper prints `not found`.
- [x] Host-side helper/example demonstrates setting and reading a key. — `tools/multiverse-config.py` via `multiverse-ctl.sh set|get|del`.

### S2.5 — Well-known keys catalogue ([#16](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/16))
*As a developer, I want a documented set of reserved keys so config is consistent across boards.*
**Acceptance criteria**
- [x] A table of reserved keys is documented (e.g. `board`, `width`, `height`, `wifi`, …) with value encodings. — see **Well-known keys** above.
- [x] Defaults are defined for when a key is absent. — per-key defaults + the "absent → compile-time/documented default" rule (no hardware auto-detection).

## Reserved-metadata candidates (deferred)

The 46 reserved bytes can absorb new per-record fields later **without moving**
key/value/seq/lengths/crc or reformatting. Deferred, in priority order:

- **`flags` byte** — e.g. read-only/locked to protect identity keys
  (`board`/`width`/`height`) from casual overwrite; likely also carries the
  log's delete marker (finalised in S2.3).
- *Rejected:* value type/encoding tag (fights "values are untyped — typing is the
  caller's job"), timestamp (no guaranteed RTC), key hash (linear scan over ≤31
  records/sector is trivial).

*(The write sequence number is now part of the format — see the `seq` field — and
the log-structured / wear-levelled persistence it enables is the S2.3 design.)*

## Out of scope

- Encryption/auth of values.
- Typed values — values stay raw bytes; typing is the caller's concern.
