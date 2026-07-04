#pragma once

// Build-time identity, injected by CMake via target_compile_definitions.
// Fallbacks keep things compiling if a build doesn't define them.
#ifndef MULTIVERSE_VERSION
#define MULTIVERSE_VERSION "unknown"
#endif

#ifndef MULTIVERSE_BOARD
#define MULTIVERSE_BOARD "unknown"
#endif

#ifndef MULTIVERSE_BOARD_ID
#define MULTIVERSE_BOARD_ID "unknown"
#endif

// USB Product string — embeds the board id so a host can identify the exact board
// (e.g. "Multiverse i75-rp2350") straight from USB metadata. Compile-time literal
// concatenation. VID/PID stay fixed for host-tooling compatibility.
#define MULTIVERSE_USB_PRODUCT "Multiverse " MULTIVERSE_BOARD_ID

namespace multiverse {
    // Firmware version, e.g. "v0.1.0-3-g69209b4" (git describe).
    inline constexpr const char *VERSION = MULTIVERSE_VERSION;
    // Compile-time board name, e.g. "i75".
    inline constexpr const char *BOARD = MULTIVERSE_BOARD;
    // Board-chip identity, e.g. "i75-rp2350".
    inline constexpr const char *BOARD_ID = MULTIVERSE_BOARD_ID;
}
