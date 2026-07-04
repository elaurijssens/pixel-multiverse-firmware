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
**Acceptance criteria**
- [ ] Boards subscribe to a multicast group (configurable via k/v).
- [ ] A multicast frame loads into each board's back buffer (E6).

### S7.4 — Synchronised flip ([#35](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/35))
*As an operator, I want all boards to show the new frame at the same time.*
**Acceptance criteria**
- [ ] A sync signal flips all subscribed boards' buffers together.
- [ ] Visible skew across boards is within an acceptable, documented bound.

## Out of scope (for now)

- TLS/auth of the command stream.
- OTA firmware update over WiFi (possible future epic).
- Discovery/management UI.

> This epic is deliberately under-specified — it is Phase 4. Treat the stories as
> placeholders to be sharpened once E1, E2, and E6 land.
