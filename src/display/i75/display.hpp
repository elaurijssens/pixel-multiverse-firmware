#pragma once

#include <cstdint>

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "hub75.hpp"

namespace display {
    // Largest supported i75 framebuffer: 16384 px × 4 B. This is the flat hub75
    // *chain* the driver renders — every panel laid out in one electrical row
    // (chain_w = panel_w · panels · , chain_h = panel_h ≤ 64, since hub75 scans
    // panel_h/2 rows via 5 address lines). Because a grid's chain pixel count
    // equals its display pixel count, the buffer cap is simply
    // display_w · display_h ≤ 16384. The logical layout (stacked/gridded panels)
    // is mapped onto this flat chain by calculation — see docs/epics/E11 and
    // display/selftest.hpp. The runtime geometry is chosen from the k/v store.
    constexpr int    MAX_PIXELS = 256 * 64;
    constexpr size_t BUFFER_MAX = static_cast<size_t>(MAX_PIXELS) * 4;

    // How a panel's grid position (col,row) maps to its sequence in the electrical
    // chain. Calibrated by eye with the layout self-test (the wiring varies).
    enum class ChainOrder : uint8_t { RASTER_TD, SERPENTINE_TD, RASTER_BU, SERPENTINE_BU };

    struct Geometry {
        int panel_w,  panel_h;    // one physical panel (all panels identical)
        int panels_x, panels_y;   // grid: panels across × down
        int display_w, display_h; // logical canvas (== panel × layout)
        int chain_w,  chain_h;    // flat hub75 chain built into the driver
        ChainOrder chain;         // grid → chain-sequence mapping
    };

    void init();
    void update();         // present the back buffer (swap front↔back, render)
    void info(std::string_view text);
    void selftest(uint8_t test_id);

    int      width();      // logical display width  (== geometry().display_w)
    int      height();     // logical display height (== geometry().display_h)
    size_t   buffer_size();// bytes the host streams: chain_w * chain_h * 4
    uint8_t* back();       // the back (hidden) framebuffer — the write/draw target
    const Geometry& geometry();
}
