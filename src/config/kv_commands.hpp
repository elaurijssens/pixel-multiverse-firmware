#pragma once

// Config commands (E2 S2.4): register put/get/del with the E1 command core so a
// host can set/read/delete persistent config over the wire.
namespace kv {

// Register the `put `/`get `/`del ` handlers with command_core. Call once at boot,
// before command_core::run() (which registers the built-in commands).
void register_commands();

} // namespace kv
