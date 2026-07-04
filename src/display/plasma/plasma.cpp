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

        // --- Strip-native self-tests (7x) -----------------------------------
        // 70: full hue spectrum, one step per LED — colour + whole-strip check.
        void render_spectrum() {
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            for (int i = 0; i < strip_len; i++) {
                RGB c = RGB::from_hsv(static_cast<float>(i) / static_cast<float>(strip_len), 1.0f, 1.0f);
                gfx->set_pen(c.r, c.g, c.b);
                gfx->pixel(Point(i, 0));
            }
        }

        // 71: a white tick every 10th LED with dim colour bands between, so the
        // length reads off as (ticks × 10) and dead LEDs stand out.
        void render_decades() {
            static const uint8_t pal[6][3] = {
                {70,0,0},{0,70,0},{0,0,70},{60,60,0},{0,60,60},{60,0,60}
            };
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            for (int i = 0; i < strip_len; i++) {
                if (i % 10 == 0) {
                    gfx->set_pen(255, 255, 255);         // decade tick
                } else {
                    const uint8_t* c = pal[(i / 10) % 6];
                    gfx->set_pen(c[0], c[1], c[2]);
                }
                gfx->pixel(Point(i, 0));
            }
        }

        // 72: first LED green, last LED red, everything else off — set the length
        // right and both ends light the physical ends (no counting).
        void render_endpoints() {
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            if (strip_len >= 1) { gfx->set_pen(0, 255, 0); gfx->pixel(Point(0, 0)); }
            if (strip_len >= 2) { gfx->set_pen(255, 0, 0); gfx->pixel(Point(strip_len - 1, 0)); }
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
        // 7x: strip-native patterns; everything else falls to the generic catalogue.
        switch (test_id) {
            case 70: render_spectrum();  update(); return;
            case 71: render_decades();   update(); return;
            case 72: render_endpoints(); update(); return;
        }
        display_selftest::render(*gfx, strip_len, 1, test_id);
        update();
    }
}
