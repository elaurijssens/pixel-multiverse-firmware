#pragma once

// Build-time identity, injected by CMake via target_compile_definitions.
// Fallbacks keep things compiling if a build doesn't define them.
#ifndef MULTIVERSE_VERSION
#define MULTIVERSE_VERSION "unknown"
#endif

#ifndef MULTIVERSE_BOARD
#define MULTIVERSE_BOARD "unknown"
#endif

namespace multiverse {
    // Firmware version, e.g. "v0.0.3-24-g69209b4" (git describe).
    inline constexpr const char *VERSION = MULTIVERSE_VERSION;
    // Compile-time board name, e.g. "i75".
    inline constexpr const char *BOARD = MULTIVERSE_BOARD;
}
