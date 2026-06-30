#pragma once

#include "command/transport.hpp"

namespace command_core {

// Runs the Multiverse command processing loop forever, reading bytes from the
// given transport: pumps the transport, waits for the `multiverse:` framing
// prefix, reads a 4-byte command id, and dispatches it to the matching handler.
// Does not return.
//
// The loop depends only on the Transport interface (S1.2) — USB CDC and, later,
// WiFi (E7) plug in without changing the core. The dispatch table comes in S1.3.
void run(Transport& transport);

} // namespace command_core
