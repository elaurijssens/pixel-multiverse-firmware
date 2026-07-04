#pragma once

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "hub75.hpp"

namespace display {
    const int WIDTH = 256;
    const int HEIGHT = 64;
    const size_t BUFFER_SIZE = WIDTH * HEIGHT * 4;

    void init();
    void update();
    void info(std::string_view text);
    void selftest(uint8_t test_id);
    extern uint8_t buffer[BUFFER_SIZE];
}