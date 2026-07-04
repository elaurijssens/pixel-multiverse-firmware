# E7 — WiFi transport & multicast sync

**Phase:** 4 · **Depends on:** E1, E2, E6 · **Unlocks:** networked multi-board displays

## Goal

Bring up WiFi on the "W" board variants as a **second transport** into the command
core (E1), so the device accepts the **same commands over the network** as over
USB. Then use **multicast** to distribute image frames and a shared **sync flip**
(E6) so many boards display in lock-step.

## Why

This is the long-term payoff: drive a wall of boards over WiFi, push frames to all
of them, and flip simultaneously. It depends on the command core being
transport-agnostic (E1), config in the k/v store (E2, incl. the WiFi enable
flag), and double buffering (E6) for the load-then-flip pattern.

## Design notes / open questions

- **Hardware:** W variants use the CYW43 wireless chip; the SDK ships
  `pico_cyw43_arch` + lwIP (present under the SDK, per CMake config output).
- **Build model — DECIDED: separate W image.** WiFi capability is a build-time
  fact: a `…-rp2350w` image (`PICO_BOARD=pico2_w`) links `pico_cyw43_arch` + lwIP;
  the `wifi` k/v key enables it at runtime, and a W image with `wifi` unset (or no
  CYW43) just doesn't bring WiFi up. **Board reality:** the **RP2350 i75 is always
  the i75W** — there is no non-wifi RP2350 i75 — so its RP2350 target becomes
  **`i75-rp2350w` (pico2_w)**, replacing the plain `i75-rp2350`. Plasma **does** have
  a non-wifi RP2350 (Plasma 2350), so it keeps `plasma-rp2350` (pico2) and gains
  `plasma-rp2350w` (pico2_w) when a 2350W is on hand. Matrix: **i75 → rp2040 +
  rp2350w; plasma → rp2040 + rp2350 (+ rp2350w later).**
- **CYW43 stability — DECIDED: `poll` mode + current SDK.** The reported CYW43/LWIP
  lockups have three known causes, all now addressed:
  1. **PIO-SPI write bug** on RP2350 (pico-sdk #2206) — *fixed & present in our SDK*.
  2. **Multicore-lockout not cleared on core reset** — *fixed & present in our SDK*
     (`multicore_lockout_victim_deinit`).
  3. **`threadsafe_background` + LWIP IRQ reentrancy** (LWIP `NO_SYS` isn't
     IRQ-safe; background services it from an IRQ → corruption under load, #1079) —
     *avoided* by using `pico_cyw43_arch_lwip_poll` and calling `cyw43_arch_poll()`
     from the command loop next to `transport.poll()` (we're single-core, on-demand).
  The earlier `i75w-multicast` prototype patched #1/#2 by hand and used background
  mode; we inherit the fixes and use poll. Init CYW43 early; keep the SDK current.
  Residual risk is empirical → **S7.1 includes a soak test** on the i75w.
- **Prior art:** `~/emma/pico/i75w-multicast` is a working i75w multicast prototype —
  a reusable reference for `lwipopts.h` (IGMP/multicast), the WiFi connect (auth
  fallback WPA3-SAE → WPA2), and the multicast UDP listener. Port to poll mode +
  our command core / k/v store rather than copying wholesale.
- **Credentials:** SSID/passphrase storage — likely k/v keys (note: 64-byte value
  limit; a passphrase fits, an enterprise cert does not). Security of stored
  creds is an open question.
- **Transport framing:** USB uses the `multiverse:` prefix; UDP/multicast has
  datagram boundaries, so framing differs. The E1 transport interface must
  accommodate both.
- **Sync method:** "each device receives image data, stores it in the back
  buffer, and on the sync command the back buffer becomes visible." Exact method
  (multicast frame + multicast flip? timestamped flip?) **remains to be
  determined.**

## User stories

### S7.1 — WiFi bring-up ([#32](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/32))
*As a board owner, I want a W board to join my network when WiFi is enabled in config.*
**Acceptance criteria**
- [x] A separate `i75-rp2350w` image (`PICO_BOARD=pico2_w`) links `pico_cyw43_arch` +
  lwIP in **poll** mode; non-W images unchanged. Green in CI.
- [x] On a W image with `wifi=1` + `ssid`/`pass`, the board connects (async);
  `cyw43_arch_poll()` runs from the command loop. Verified: i75w associates + pulls a
  DHCP lease, non-blocking.
- [x] WiFi unset/disabled (or no CYW43) ⇒ behaves as a non-WiFi board (early return
  before `cyw43_arch_init`; non-W images compile no-op stubs).
- [x] Connection status via `vers`/`diag` (state + IP). Secret keys (`pass`) masked
  in the `keys` dump.
- [ ] **Soak test:** WiFi associated and serviced over an extended run without lockup
  (initial stability good across reflashes + queries; long soak still to run).

### S7.2 — Commands over the network ([#33](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/33))
*As a host, I want to send the same commands over WiFi as over USB.*
**Acceptance criteria**
- [x] `net/udp_transport` implements the E1 `Transport` interface (byte-stream reassembled
  from datagrams, poll mode); `command_core::run()` services USB + UDP together, dispatching
  to the same handlers unchanged.
- [x] Verified on the i75w over UDP: `test` (control), `vers` (query round-trip) and a full
  256×64 `data` frame (image, `last_rx=65536`) — concurrent with USB. `flip`/`hold`/`live`
  (sync) use the same path.

> Note: raw UDP is lossy — a full frame needs the 72 KB reassembly ring and, ultimately, a
> chunk/sequence scheme for reliability + speed. That reliability layer is **S7.3**.

### S7.3 — Multicast frame distribution ([#34](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/34))
*As an operator, I want to send one frame to many boards at once.*

Raw multicast UDP is lossy and unackable (one sender → many boards), so frames use a
**self-describing chunk protocol**, not the S7.2 byte-stream (where one lost datagram
desyncs everything). Each datagram carries a header + chunk; the receiver places chunks
by offset, so a lost chunk is just a gap at a known place.

- **Subscribe:** join an IPv4 multicast group (IGMP) — `mgroup` (e.g. `239.255.0.1`) +
  `mport` from the k/v store; a separate `udp_pcb` from the S7.2 command socket.
- **Frame chunk** (little-endian): `magic(4) frame_id(2) flags(2) total_len(4) offset(4)`
  header + chunk bytes. Receiver writes each chunk into `display::back()` at `offset`,
  tracks bytes received for the current `frame_id`; `received == total_len` ⇒ complete.
- **Best-effort:** a new `frame_id` before the old completes abandons the incomplete
  frame (never shown). With E6 double-buffering the previous complete frame stays up —
  no tearing, no stale-repair (right for real-time animation).
- **Present** reuses E6: on completion, `live` flips immediately; `hold` leaves it in the
  back buffer for a flip — the hook **S7.4** drives with a multicast sync flip.

**Acceptance criteria**
- [x] Board joins the `mgroup`/`mport` group from the k/v store (`igmp_joingroup`, W builds).
- [x] A chunked frame reassembles by offset into `display::back()` and presents (respecting
  `hold`/`live`); a new `frame_id` abandons an incomplete one. Verified on both i75w's via
  the identical receive path — the socket is bound `IP_ANY:mport`, so unicast `MVF1` frames
  drive it too (green→board 1, red→board 2).
- [x] **Multi-board streaming verified:** two i75w's rendered the same 18–24-frame
  animation in sync from a single sender and both stayed fully responsive throughout
  (raw diag). (An earlier apparent "hang" was a test-harness artifact — false-negative
  responsiveness checks + the display holding the last frame + a board on an older build.)
- [x] True one-to-many **multicast delivery** (one packet → many) — **verified on a flat
  segment.** The IoT SSID is one `192.168.12.0/22` L2 segment (netmask `255.255.252.0`), not
  the per-client /24s it looked like: the sender (`.14.186`) and both boards (`.15.147`,
  `.13.174`) share it and resolve each other via ARP on the same interface. A single
  multicast sender to `239.255.0.1:54322` drove **both** boards to the same solid colour
  together. (The earlier "different subnets, doesn't route" call was a /24 misread — at /22
  the `.12`–`.15` ranges are one subnet.)
  **Caveat — lossy:** raw multicast drops chunks, so an individual frame often fails to
  complete (a single red/green/blue cycle held on the last complete frame); reliable
  animation needs **redundant sends** or the S7.4 sync/flip layer. Verified by repeating a
  frame ~25× — both boards then held solid green.

**Hardening from the investigation:** lwIP uses fixed pools (no libc-malloc fragmentation
under per-datagram pbuf churn); a command-loop watchdog is in as a safety net.

### S7.4 — Synchronised flip ([#35](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/35))
*As an operator, I want all boards to show the new frame at the same time.*

**Mechanism.** The MVF1 header's `flags` field gains a **FLIP bit** (`0x0001`): a datagram
with FLIP set and no payload tells every board to present its back buffer. Host pattern:
`hold` each board → unicast each its frame slice → send one **multicast FLIP** to the group,
so a single packet flips a whole wall. Two safeguards: a **completion gate** (a flip only
presents a *fully-received* frame — a partial/mid-load buffer is held, never torn) and a
**redundant flip** (sent a few times, since multicast is unacked). A companion **zlib frame
flag** (`0x0002`) shrinks frames ~6× (host `zlib.compress` → board `uncompress` into back()),
and the receiver **dedups chunk offsets** so a frame can be sent 2× for loss resilience.

**Acceptance criteria**
- [x] A sync signal flips all subscribed boards' buffers together. Verified: `hold` both
  boards, unicast a solid colour to each, one multicast FLIP → both snap to it together.
- [x] Visible skew across boards is within an acceptable, documented bound **for low-rate
  synchronised content**. **Bound (measured, documented below): the multicast flip suits
  slideshow / periodic-update walls, not per-frame video.**

**Finding — flip is for low-rate sync; use live mode for video.** Instrumented the receiver
(`mframes`/`mdrop`/`mflip_ok`/`mflip_gate`, exposed via `diag`) and streamed a 256×128 h264
clip at 24 fps split across two i75w. Results: **0 air loss** (`mdrop=0`), decompress ~14 ms,
but in **flip (hold) mode only ~15 fps actually present** — ~30 % of flips are *gated*
because a completed frame's flip lands after the **next** frame has begun overwriting the
back buffer (a load-then-separate-flip race that widens with frame rate). A bigger guard made
it *worse*, and unicast-vs-multicast flip barely moved it — so it is neither decompression nor
AP/DTIM latency. In **live mode** (present on completion, no flip) the same clip presents the
full **24 fps, 0 drops**, over immediate unicast, with only a few-ms host send-order skew.
**So: `multiverse-wall.py` streams video in live mode (`--no-sync`); the multicast sync flip
is reserved for low-rate synchronised presentation.** Host tooling (compression, redundancy,
per-frame deadline pacing, still-frame re-send, video via ffmpeg) lives in `tools/multiverse-wall.py`.

## Out of scope (for now)

- TLS/auth of the command stream.
- OTA firmware update over WiFi (possible future epic).
- Discovery/management UI.

> This epic is deliberately under-specified — it is Phase 4. Treat the stories as
> placeholders to be sharpened once E1, E2, and E6 land.
