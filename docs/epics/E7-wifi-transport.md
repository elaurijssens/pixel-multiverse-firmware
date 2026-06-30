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
- **Compile in, toggle via k/v:** per the vision, WiFi support may be compiled
  into the image and **enabled/disabled via a k/v key** (E2) rather than a
  separate build. Confirm the RAM/flash cost of always compiling it in.
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

## User stories (provisional — refine when the epic starts)

### S7.1 — WiFi bring-up ([#32](https://github.com/elaurijssens/gu-multiverse/issues/32))
*As a board owner, I want a W board to join my network when WiFi is enabled in config.*
**Acceptance criteria**
- [ ] Device connects using credentials/flags from the k/v store.
- [ ] WiFi disabled (or absent config) ⇒ device behaves exactly as a non-WiFi board.
- [ ] Connection status visible via the diagnostic console (E5).

### S7.2 — Commands over the network ([#33](https://github.com/elaurijssens/gu-multiverse/issues/33))
*As a host, I want to send the same commands over WiFi as over USB.*
**Acceptance criteria**
- [ ] A network transport implements the E1 interface; existing handlers work unchanged.
- [ ] At least the image and sync commands work over the network.

### S7.3 — Multicast frame distribution ([#34](https://github.com/elaurijssens/gu-multiverse/issues/34))
*As an operator, I want to send one frame to many boards at once.*
**Acceptance criteria**
- [ ] Boards subscribe to a multicast group (configurable via k/v).
- [ ] A multicast frame loads into each board's back buffer (E6).

### S7.4 — Synchronised flip ([#35](https://github.com/elaurijssens/gu-multiverse/issues/35))
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
