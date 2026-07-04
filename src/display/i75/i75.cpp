#include "display.hpp"
#include "version.hpp"
#include "../selftest.hpp"
#include "config/kv_flash.hpp"

#include <cstddef>
#include <cstdint>

using namespace pimoroni;

namespace display {
    // Framebuffer sized for the largest supported mode; the graphics/panel are
    // constructed in init() for the runtime dimensions read from the k/v store.
    uint8_t buffer[BUFFER_MAX];

    namespace {
        int panel_w = 256;   // runtime, set in init()
        int panel_h = 64;
        PicoGraphics_PenRGB888* gfx   = nullptr;
        Hub75*                  panel = nullptr;

        // Read an ASCII-decimal dimension key, validated to [lo, hi]. Sets `bad`
        // and returns `def` when the value is present but unparseable/out of range.
        int read_dim(const char* key, int def, int lo, int hi, bool& bad) {
            size_t vlen = 0;
            const uint8_t* v = kv::config().get(key, vlen);
            if (v == nullptr || vlen == 0) return def;  // absent → default (not "bad")
            long n = 0;
            for (size_t i = 0; i < vlen; i++) {
                if (v[i] < '0' || v[i] > '9') { bad = true; return def; }
                n = n * 10 + (v[i] - '0');
                if (n > 100000) { bad = true; return def; }
            }
            if (n < lo || n > hi) { bad = true; return def; }
            return static_cast<int>(n);
        }

        // Choose the panel dimensions from config. Returns true if the config was
        // present but invalid (so the caller can show a diagnostic); dimensions
        // always fall back to a safe 256×64.
        bool load_dims() {
            bool bad = false;
            int w = read_dim("width",  256, 16, 256, bad);
            int h = read_dim("height",  64, 16, 128, bad);
            if (static_cast<size_t>(w) * h > static_cast<size_t>(MAX_PIXELS)) {
                bad = true;  // e.g. 256×128 — exceeds the framebuffer
                w = 256; h = 64;
            }
            panel_w = w;
            panel_h = h;
            return bad;
        }
    }

    int    width()       { return panel_w; }
    int    height()      { return panel_h; }
    size_t buffer_size() { return static_cast<size_t>(panel_w) * panel_h * 4; }

    void __isr dma_complete() {
        panel->dma_complete();
    }

    void init() {
        bool bad_cfg = load_dims();  // needs config_boot() to have run (see main.cpp)

        gfx   = new PicoGraphics_PenRGB888(panel_w, panel_h, buffer);
        panel = new Hub75(panel_w, panel_h, nullptr, PANEL_GENERIC, false, Hub75::COLOR_ORDER::RGB);
        panel->start(dma_complete);

        if (bad_cfg) {
            info("cfg: bad width/height");  // diagnostic, then usable at the fallback size
        } else {
            selftest(display_selftest::INFO_SCREEN);  // boot screen = firmware version
        }
    }

    void info(std::string_view text) {
        gfx->set_pen(0, 0, 0);
        gfx->clear();
        gfx->set_pen(255, 255, 255);
        gfx->set_font("bitmap8");
        gfx->text(text, Point(0, 0), panel_w, 1);
        update();
    }

    void update() {
        panel->update(gfx);
    }

    void selftest(uint8_t test_id) {
        // Test 41 shows the real info() screen; everything else is a pattern drawn
        // straight into the framebuffer at the configured dimensions.
        if (test_id == display_selftest::INFO_SCREEN) {
            info(MULTIVERSE_VERSION);
            return;
        }
        display_selftest::render(*gfx, panel_w, panel_h, test_id);
        update();
    }
}
