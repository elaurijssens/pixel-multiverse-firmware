#pragma once

#include "command/transport.hpp"

namespace command_core {

// A command handler pulls its own payload from the transport and acts on it.
// Plain function pointer (not std::function) to avoid heap/code-size cost on
// the RP2040/RP2350.
using Handler = void (*)(Transport&);

// Register a handler for a 4-byte command id. Returns false if the id is already
// registered or the table is full. Built-in commands self-register inside run();
// later modules (e.g. E2's k/v commands) call this before run() to add their own.
bool register_command(const char id[4], Handler handler);

// Runs the Multiverse command processing loop forever, reading bytes from the
// given transport: pumps the transport, waits for the `multiverse:` framing
// prefix, reads a 4-byte command id, and dispatches it to the registered
// handler. Unknown ids are skipped and the loop re-syncs on the next prefix.
// Does not return.
//
// The loop depends only on the Transport interface (S1.2) — USB CDC and, later,
// WiFi (E7) plug in without changing the core.
void run(Transport& transport);

} // namespace command_core
