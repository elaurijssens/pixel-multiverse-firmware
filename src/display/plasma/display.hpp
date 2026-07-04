#pragma once

#include <cstdint>

#include "libraries/pico_graphics/pico_graphics.hpp"

namespace display {
    // Plasma drives a linear WS2812 strip: a width×1 PenRGB888 framebuffer of at
    // most MAX_LEDS pixels. update() walks the framebuffer into the WS2812 driver;
    // because the strip latches and holds, frames are pushed on demand (no
    // background refresh — so none of the refresh-vs-USB contention of a matrix).
    // The length is read from the k/v store at boot (see plasma.cpp).
    constexpr int    MAX_LEDS   = 1024;
    constexpr size_t BUFFER_MAX = static_cast<size_t>(MAX_LEDS) * 4;

    void init();
    void update();         // present the back buffer (swap front↔back, push to strip)
    void info(std::string_view text);
    void selftest(uint8_t test_id);

    int      width();      // configured strip length (from k/v, default 64)
    int      height();     // always 1 — a linear strip
    size_t   buffer_size();// width() * 4 — bytes the host streams
    uint8_t* back();       // the back (hidden) framebuffer — the write/draw target
}
