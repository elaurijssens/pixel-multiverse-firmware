#pragma once

#include "command/transport.hpp"

// UDP command transport (E7 S7.2): the same command core, framing and handlers,
// but bytes arrive in UDP datagrams instead of over USB CDC. Reassembled into a
// byte stream so existing handlers work unchanged. Poll mode → the lwIP receive
// callback runs inside cyw43_arch_poll() (no IRQ), so the buffer needs no locks.

namespace net {
    // The singleton UDP transport, or nullptr on non-W builds (no lwIP). Pass to
    // command_core::run() as the secondary transport.
    command_core::Transport* udp_transport();

    // Bind the UDP command socket to the `port` k/v key (default 54321). Call once
    // after WiFi is up; a no-op if WiFi isn't enabled or on non-W builds.
    void udp_transport_init();
}
