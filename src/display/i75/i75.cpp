#include "display.hpp"
#include "version.hpp"
#include "../selftest.hpp"

using namespace pimoroni;

namespace display {
    uint8_t buffer[BUFFER_SIZE];
    PicoGraphics_PenRGB888 graphics(WIDTH, HEIGHT, &buffer);
    Hub75 hub75(WIDTH, HEIGHT, nullptr, PANEL_GENERIC, false, Hub75::COLOR_ORDER::RGB);

    void __isr dma_complete() {
        hub75.dma_complete();
    }

    void init() {
        hub75.start(dma_complete);

        // Boot screen is the info self-test (currently the firmware version).
        selftest(display_selftest::INFO_SCREEN);
    }

    void info(std::string_view text) {
        graphics.set_pen(0, 0, 0);
        graphics.clear();
        graphics.set_pen(255, 255, 255);
        graphics.set_font("bitmap8");
        graphics.text(text, Point(0, 0), WIDTH, 1);
        update();
    }

    void update() {
        hub75.update(&graphics);
    }

    void selftest(uint8_t test_id) {
        // Test 41 shows the real info() screen; everything else is a pattern
        // drawn straight into the framebuffer.
        if (test_id == display_selftest::INFO_SCREEN) {
            info(MULTIVERSE_VERSION);
            return;
        }
        display_selftest::render(graphics, WIDTH, HEIGHT, test_id);
        update();
    }
}