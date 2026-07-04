#include "display.hpp"
#include "version.hpp"
#include "../selftest.hpp"
#include "config/kv_flash.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

using namespace pimoroni;

namespace display {
    // Framebuffer sized for the largest supported chain; the graphics/panel are
    // constructed in init() for the runtime geometry read from the k/v store.
    uint8_t buffer[BUFFER_MAX];

    namespace {
        // Safe, always-renderable default: a single flat 256×64 panel.
        Geometry geo = { 256, 64, 1, 1, 256, 64, 256, 64, ChainOrder::RASTER_TD };
        PicoGraphics_PenRGB888* gfx   = nullptr;
        Hub75*                  panel = nullptr;

        // Parsed value of an ASCII-decimal key: >=0 value, -1 absent, -2 invalid.
        long read_opt(const char* key) {
            size_t vlen = 0;
            const uint8_t* v = kv::config().get(key, vlen);
            if (v == nullptr || vlen == 0) return -1;  // absent
            long n = 0;
            for (size_t i = 0; i < vlen; i++) {
                if (v[i] < '0' || v[i] > '9') return -2;  // non-digit
                n = n * 10 + (v[i] - '0');
                if (n > 100000) return -2;  // absurd
            }
            return n;
        }

        // New key, else the legacy key, else the default. Flags a present-but-
        // invalid value as bad (so the caller shows a diagnostic).
        int resolve(const char* primary, const char* legacy, int def, bool& bad) {
            long n = read_opt(primary);
            if (n == -1 && legacy != nullptr) n = read_opt(legacy);
            if (n == -1) return def;              // absent everywhere → default
            if (n < 0) { bad = true; return def; }  // -2 → invalid
            return static_cast<int>(n);
        }

        ChainOrder parse_chain(bool& bad) {
            size_t vlen = 0;
            const uint8_t* v = kv::config().get("chain", vlen);
            if (v == nullptr || vlen == 0) return ChainOrder::RASTER_TD;  // default
            std::string_view s(reinterpret_cast<const char*>(v), vlen);
            if (s == "raster-td")     return ChainOrder::RASTER_TD;
            if (s == "serpentine-td") return ChainOrder::SERPENTINE_TD;
            if (s == "raster-bu")     return ChainOrder::RASTER_BU;
            if (s == "serpentine-bu") return ChainOrder::SERPENTINE_BU;
            bad = true;
            return ChainOrder::RASTER_TD;
        }

        // Read panel/layout/display config into `geo`. Returns true if the config
        // was present but invalid (caller shows a diagnostic); `geo` is always left
        // at a renderable geometry, falling back to a flat 256×64 panel.
        bool load_geometry() {
            bool bad = false;
            // Panel size falls back to the legacy width/height keys (a single 1×1
            // panel), then to 256×64 — so existing configs keep working.
            int pw = resolve("panel_w",  "width",  256, bad);
            int ph = resolve("panel_h",  "height",  64, bad);
            int nx = resolve("panels_x", nullptr,    1, bad);
            int ny = resolve("panels_y", nullptr,    1, bad);
            int dw = resolve("disp_w", nullptr, pw * nx, bad);  // k/v keys are <= 8 bytes
            int dh = resolve("disp_h", nullptr, ph * ny, bad);
            ChainOrder order = parse_chain(bad);

            // Range, consistency and buffer checks — see docs/epics/E11.
            if (pw < 8 || pw > 256 || ph < 8 || ph > 64 || (ph & 1) != 0) bad = true;  // hub75 scans ph/2 ≤ 32 rows
            if (nx < 1 || nx > 64 || ny < 1 || ny > 64)                   bad = true;
            if (dw != pw * nx || dh != ph * ny)                           bad = true;  // display == panel × layout
            if (static_cast<size_t>(dw) * dh > static_cast<size_t>(MAX_PIXELS)) bad = true;  // fits the framebuffer

            if (bad) {  // safe fallback
                pw = 256; ph = 64; nx = 1; ny = 1; dw = 256; dh = 64; order = ChainOrder::RASTER_TD;
            }

            geo.panel_w = pw;  geo.panel_h = ph;
            geo.panels_x = nx; geo.panels_y = ny;
            geo.display_w = dw; geo.display_h = dh;
            geo.chain_w = pw * nx * ny;  // every panel in one electrical row
            geo.chain_h = ph;
            geo.chain = order;
            return bad;
        }
    }

    int    width()       { return geo.display_w; }
    int    height()      { return geo.display_h; }
    size_t buffer_size() { return static_cast<size_t>(geo.chain_w) * geo.chain_h * 4; }
    const Geometry& geometry() { return geo; }

    void __isr dma_complete() {
        panel->dma_complete();
    }

    void init() {
        bool bad_cfg = load_geometry();  // needs config_boot() to have run (see main.cpp)

        // Graphics + panel are built at the flat chain geometry; the logical layout
        // is applied when rendering self-tests (E11 S11.2) and by the host for
        // streamed frames.
        gfx   = new PicoGraphics_PenRGB888(geo.chain_w, geo.chain_h, buffer);
        panel = new Hub75(geo.chain_w, geo.chain_h, nullptr, PANEL_GENERIC, false, Hub75::COLOR_ORDER::RGB);
        panel->start(dma_complete);

        if (bad_cfg) {
            info("cfg: bad geometry");  // diagnostic, then usable at the fallback size
        } else {
            selftest(display_selftest::INFO_SCREEN);  // boot screen = firmware version
        }
    }

    void info(std::string_view text) {
        gfx->set_pen(0, 0, 0);
        gfx->clear();
        gfx->set_pen(255, 255, 255);
        gfx->set_font("bitmap8");
        gfx->text(text, Point(0, 0), geo.chain_w, 1);
        update();
    }

    void update() {
        panel->update(gfx);
    }

    void selftest(uint8_t test_id) {
        // Test 41 shows the real info() screen; everything else is a pattern drawn
        // into the flat chain framebuffer. (Layout-aware mapping arrives in S11.2.)
        if (test_id == display_selftest::INFO_SCREEN) {
            info(MULTIVERSE_VERSION);
            return;
        }
        display_selftest::render(*gfx, geo.chain_w, geo.chain_h, test_id);
        update();
    }
}
