#pragma once

namespace command_core {

// Runs the Multiverse command processing loop forever: pumps USB, waits for the
// `multiverse:` framing prefix, reads a 4-byte command id, and dispatches it to
// the matching handler. Does not return.
//
// This is the transport-agnostic spine the later E1 stories build on (transport
// interface in S1.2, dispatch table in S1.3). For S1.1 it is a verbatim
// extraction of the loop that previously lived inline in `main()`.
void run();

} // namespace command_core
