#pragma once

#include <cstdint>
#include <string_view>

#include "config/kv_store.hpp"

// Board descriptor built at boot from the E2 k/v store (E3 S3.1).
//
// This is the single source of the per-board facts that today live scattered
// across the four src/display/<board>/*.cpp files. It is pure, SDK-free data so
// it host-compiles and unit-tests; later E3 stories consume it to drive one
// unified display implementation (S3.2), dispatch to the right driver (S3.3),
// size the framebuffer (S3.4), and validate i75/Plasma dimensions (S3.5).
namespace display {

enum class BoardKind {
    Galactic,   // GalacticUnicorn 53x11
    Cosmic,     // CosmicUnicorn   32x32
    Stellar,    // StellarUnicorn  16x16
    I75,        // Interstate 75 / 75W, Hub75 — configurable dims
    Plasma,     // WS2812B strip — width 1, configurable length
    Unknown,
};

struct BoardDescriptor {
    BoardKind   board;      // also selects the driver (S3.3)
    int         width;
    int         height;
    const char* font;       // "bitmap5" (Unicorns) or "bitmap8" (i75)
    bool        has_audio;  // Unicorns have synth/sample; i75/Plasma do not
    bool        invalid;    // true if config was unusable and we fell back safely
};

// Map a board name (from the `board` k/v value or multiverse::BOARD) to a kind.
// Unrecognised names return BoardKind::Unknown.
BoardKind parse_board_kind(std::string_view name);

// Build the descriptor from `store`, using `fallback` when the `board` key is
// absent (and keeping it, with invalid=true, when the key is present but
// unparseable). i75/Plasma read width/height from the store; Unicorn dims are
// fixed. Never asserts — invalid config fails safe.
BoardDescriptor describe(kv::Store& store, BoardKind fallback);

} // namespace display
