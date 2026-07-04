#pragma once

// WiFi bring-up for the W images (E7 S7.1). On non-W builds every function is a
// no-op stub, so callers (main, command_core) need no #if guards.
//
// Config keys (k/v store): `wifi` (1/yes/on to enable), `ssid`, `pass`, and an
// optional `auth` (wpa2 [default] / wpa3 / mixed / open). CYW43 runs in POLL mode:
// wifi_poll() must be called from the command loop (see docs/epics/E7).

namespace net {
    // Read config and, if enabled, init the CYW43 and start an async connect.
    // No-op when `wifi` is unset/false or there is no CYW43 (non-W image).
    void wifi_init();

    // Service the CYW43 + lwIP stack. Call each command-loop iteration.
    void wifi_poll();

    bool wifi_enabled();          // configured + CYW43 init succeeded
    bool wifi_connected();        // link currently up (live)
    const char* wifi_ip();        // dotted IPv4 once DHCP completes, else ""
}
