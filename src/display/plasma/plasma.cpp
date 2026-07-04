#include "display.hpp"
#include "version.hpp"
#include "../selftest.hpp"
#include "config/kv_flash.hpp"

#include "ws2812.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace pimoroni;

namespace display {
    // Framebuffer sized for the longest supported strip; the graphics + WS2812
    // driver are constructed in init() for the runtime length from the k/v store.
    uint8_t buffer[BUFFER_MAX];

    namespace {
        constexpr uint LED_PIN = 15;   // PLASMA2350 DATA pin (WS2812)
        int strip_len = 64;            // runtime, set in init()
        PicoGraphics_PenRGB888* gfx   = nullptr;
        plasma::WS2812*         strip = nullptr;

        // Read the strip length from the k/v store. Returns true if the stored
        // value was present but invalid (caller shows a diagnostic); `strip_len`
        // always ends up in [1, MAX_LEDS], falling back to 64.
        bool load_length() {
            bool bad = false;
            size_t vlen = 0;
            const uint8_t* v = kv::config().get("length", vlen);
            int n = 64;
            if (v != nullptr && vlen > 0) {
                long parsed = 0;
                bool ok = true;
                for (size_t i = 0; i < vlen; i++) {
                    if (v[i] < '0' || v[i] > '9') { ok = false; break; }
                    parsed = parsed * 10 + (v[i] - '0');
                    if (parsed > MAX_LEDS) { ok = false; break; }
                }
                if (ok && parsed >= 1) n = static_cast<int>(parsed);
                else bad = true;
            }
            strip_len = n;
            return bad;
        }

        // WS2812 wire order from the k/v store. Defaults to GRB (standard WS2812B);
        // some strips are RGB or other — set the `order` key to match.
        plasma::WS2812::COLOR_ORDER parse_order() {
            using CO = plasma::WS2812::COLOR_ORDER;
            size_t vlen = 0;
            const uint8_t* v = kv::config().get("order", vlen);
            if (v == nullptr || vlen == 0) return CO::GRB;
            std::string_view s(reinterpret_cast<const char*>(v), vlen);
            if (s == "rgb") return CO::RGB;
            if (s == "rbg") return CO::RBG;
            if (s == "grb") return CO::GRB;
            if (s == "gbr") return CO::GBR;
            if (s == "brg") return CO::BRG;
            if (s == "bgr") return CO::BGR;
            return CO::GRB;  // unknown → default
        }
    }

    int    width()       { return strip_len; }
    int    height()      { return 1; }
    size_t buffer_size() { return static_cast<size_t>(strip_len) * 4; }

    void init() {
        bool bad_cfg = load_length();  // needs config_boot() to have run (see main.cpp)

        gfx   = new PicoGraphics_PenRGB888(strip_len, 1, buffer);
        strip = new plasma::WS2812(strip_len, pio0, 0, LED_PIN,
                                   plasma::WS2812::DEFAULT_SERIAL_FREQ, false,
                                   parse_order());

        // A strip can't render text, so the boot state is a single dim indicator
        // on LED 0: green = ready, red = bad length config (running at fallback 64).
        gfx->set_pen(0, 0, 0);
        gfx->clear();
        gfx->set_pen(bad_cfg ? 60 : 0, bad_cfg ? 0 : 40, 0);
        gfx->pixel(Point(0, 0));
        update();
    }

    void info(std::string_view text) {
        // Best-effort on a 1-tall strip: render the text into the framebuffer so a
        // command still produces a visible change, then push it.
        gfx->set_pen(0, 0, 0);
        gfx->clear();
        gfx->set_pen(255, 255, 255);
        gfx->set_font("bitmap8");
        gfx->text(text, Point(0, 0), strip_len, 1);
        update();
    }

    void update() {
        // Framebuffer is PenRGB888: little-endian B,G,R,0 per pixel. Walk it into
        // the WS2812 driver and push (blocking, so the transfer completes).
        for (int i = 0; i < strip_len; i++) {
            uint8_t b = buffer[i * 4 + 0];
            uint8_t g = buffer[i * 4 + 1];
            uint8_t r = buffer[i * 4 + 2];
            strip->set_rgb(i, r, g, b);
        }
        strip->update(true);
    }

    void selftest(uint8_t test_id) {
        // No info() screen on a strip: INFO_SCREEN just re-shows the ready indicator.
        if (test_id == display_selftest::INFO_SCREEN) {
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            gfx->set_pen(0, 40, 0);
            gfx->pixel(Point(0, 0));
            update();
            return;
        }
        display_selftest::render(*gfx, strip_len, 1, test_id);
        update();
    }
}
