#pragma once

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "hub75.hpp"

namespace display {
    // Largest supported i75 framebuffer: 16384 px × 4 B. These are the raw hub75
    // *chain* dimensions (width ≤ 256, height ≤ 64 — the driver's 5 address lines
    // scan 32×2 = 64 rows), not the logical display: stacked panels (e.g. a
    // 128×128 built from two 128×64) are configured here as their chain geometry
    // (256×64) and folded to the logical layout host-side by the pixel-multiverse
    // library. The actual dimensions are chosen at runtime from the k/v store
    // (E9 S9.4/S9.5), so `buffer` is sized for the maximum and
    // `width()`/`height()`/`buffer_size()` report the configured mode.
    constexpr int    MAX_PIXELS = 256 * 64;
    constexpr size_t BUFFER_MAX = static_cast<size_t>(MAX_PIXELS) * 4;

    void init();
    void update();
    void info(std::string_view text);
    void selftest(uint8_t test_id);

    int    width();        // configured panel width  (from k/v, default 256)
    int    height();       // configured panel height (from k/v, default 64)
    size_t buffer_size();  // width() * height() * 4

    extern uint8_t buffer[BUFFER_MAX];
}
