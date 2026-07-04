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
  fact: build a distinct `…-rp2350w` image (`PICO_BOARD=pico2_w`, linking
  `pico_cyw43_arch` + lwIP), keeping the non-W `…-rp2350` images WiFi-free. The
  `wifi` k/v key **enables** it at runtime on a W image; a W image run on hardware
  without the CYW43 (or `wifi` unset) simply doesn't bring WiFi up. This matches
  the per-board-chip image model (S9.6) — the W variants are extra CI targets.
- **CYW43 stability — DECIDED: `poll` mode.** The CYW43/LWIP lockups reported in
  the ecosystem are largely a `threadsafe_background` failure class: LWIP in
  `NO_SYS` mode isn't IRQ-reentrant, but background mode services it from an IRQ,
  so under load an IRQ mid-LWIP corrupts its state → hangs (pico-sdk #1079). We are
  **single-core with an on-demand main loop**, so we use
  `pico_cyw43_arch_lwip_poll` and call `cyw43_arch_poll()` from the command loop
  next to `transport.poll()` — no IRQ servicing, no reentrancy. Init CYW43 early
  (before the loop); keep pico-sdk + `cyw43_driver` current. Residual risk is
  empirical → **S7.1 includes a soak test** on the i75w.
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
- [ ] A separate `…-rp2350w` image (`PICO_BOARD=pico2_w`) links `pico_cyw43_arch` +
  lwIP in **poll** mode; non-W images are unchanged. Added to the CI matrix.
- [ ] On a W image with `wifi=1` + `ssid`/`pass` k/v keys, the board connects;
  `cyw43_arch_poll()` runs from the command loop next to `transport.poll()`.
- [ ] WiFi unset/disabled (or no CYW43) ⇒ behaves exactly as a non-WiFi board.
- [ ] Connection status queryable — extend `vers`/`diag` with WiFi state + IP.
- [ ] **Soak test:** WiFi associated and serviced over an extended run without lockup
  (the CYW43 stability check).

### S7.2 — Commands over the network ([#33](https://github.com/elaurijssens/pixel-multiverse-firmware/issues/33))
*As a host, I want to send the same commands over WiFi as over USB.*
**Acceptance criteria**
- [ ] A network transport implements the E1 interface; existing handlers work unchanged.
- [ ] At least the image and sync commands work over the network.

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
