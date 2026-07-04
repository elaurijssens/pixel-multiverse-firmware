#include "display.hpp"
#include "version.hpp"
#include "../selftest.hpp"
#include "config/kv_flash.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>

using namespace pimoroni;

namespace display {
    namespace {
        // Safe, always-renderable default: a single flat 256×64 panel.
        Geometry geo = { 256, 64, 1, 1, 256, 64, 256, 64, ChainOrder::RASTER_TD };

        // Two framebuffers (heap, runtime-sized): pg[front] is shown, pg[1-front]
        // is the write/draw target. `gfx` always tracks the back gfx so all drawing
        // code targets the hidden buffer. If the back allocation doesn't fit
        // (RP2040 near max dims), we fall back to single-buffered (pg[1]=pg[0]).
        uint8_t*                buf[2] = { nullptr, nullptr };
        PicoGraphics_PenRGB888* pg[2]  = { nullptr, nullptr };
        int                     front  = 0;
        bool                    dbuf   = false;
        PicoGraphics_PenRGB888* gfx    = nullptr;  // current back (draw target)
        Hub75*                  panel  = nullptr;

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

        // Hub75 wire colour order from the k/v `order` key (same key as plasma).
        // Defaults to RGB (today's hard-coded value); some panels wire channels
        // differently.
        Hub75::COLOR_ORDER parse_order() {
            using CO = Hub75::COLOR_ORDER;
            size_t vlen = 0;
            const uint8_t* v = kv::config().get("order", vlen);
            if (v == nullptr || vlen == 0) return CO::RGB;
            std::string_view s(reinterpret_cast<const char*>(v), vlen);
            if (s == "rgb") return CO::RGB;
            if (s == "rbg") return CO::RBG;
            if (s == "grb") return CO::GRB;
            if (s == "gbr") return CO::GBR;
            if (s == "brg") return CO::BRG;
            if (s == "bgr") return CO::BGR;
            return CO::RGB;  // unknown → default
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

        // --- Layout-aware rendering (E11 S11.2) ------------------------------
        // Grid position (pcol,prow) → the panel's index in the electrical chain.
        // A single row keeps seq = pcol, so a 1×N horizontal layout is identity
        // and the flat-chain patterns are unchanged.
        int chain_seq(int pcol, int prow, int cols, int rows, ChainOrder order) {
            bool bottom_up = (order == ChainOrder::RASTER_BU || order == ChainOrder::SERPENTINE_BU);
            bool serp      = (order == ChainOrder::SERPENTINE_TD || order == ChainOrder::SERPENTINE_BU);
            int chainrow   = bottom_up ? (rows - 1 - prow) : prow;
            int col_in_row = (serp && (chainrow & 1)) ? (cols - 1 - pcol) : pcol;
            return chainrow * cols + col_in_row;
        }

        // Logical display (lx,ly) → flat chain framebuffer (cx,cy).
        void map_to_chain(int lx, int ly, int& cx, int& cy) {
            int pcol = lx / geo.panel_w, px = lx % geo.panel_w;
            int prow = ly / geo.panel_h, py = ly % geo.panel_h;
            int seq = chain_seq(pcol, prow, geo.panels_x, geo.panels_y, geo.chain);
            cx = seq * geo.panel_w + px;
            cy = py;
        }

        void plot_logical(int lx, int ly) {
            int cx, cy;
            map_to_chain(lx, ly, cx, cy);
            gfx->pixel(Point(cx, cy));
        }

        // Layout self-test: each panel filled a distinct shade + white border,
        // labelled with its (col,row) and chain seq — so the physical arrangement
        // and the `chain` order can be confirmed and calibrated by eye.
        void render_layout() {
            static const uint8_t pal[8][3] = {
                {40,40,40},{0,55,75},{70,45,0},{45,0,70},
                {0,60,0},{70,0,45},{0,45,70},{65,65,0}
            };
            const int pw = geo.panel_w, ph = geo.panel_h;
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            gfx->set_font("bitmap8");
            for (int prow = 0; prow < geo.panels_y; prow++) {
                for (int pcol = 0; pcol < geo.panels_x; pcol++) {
                    int seq = chain_seq(pcol, prow, geo.panels_x, geo.panels_y, geo.chain);
                    int x0 = seq * pw;
                    const uint8_t* c = pal[seq % 8];
                    gfx->set_pen(c[0], c[1], c[2]);
                    gfx->rectangle(Rect(x0, 0, pw, ph));
                    gfx->set_pen(255, 255, 255);
                    gfx->rectangle(Rect(x0,        0,      pw, 1));
                    gfx->rectangle(Rect(x0,        ph - 1, pw, 1));
                    gfx->rectangle(Rect(x0,        0,      1,  ph));
                    gfx->rectangle(Rect(x0 + pw-1, 0,      1,  ph));
                    gfx->text(std::to_string(pcol) + "," + std::to_string(prow),
                              Point(x0 + 2, 2), pw, 1.0f);
                    if (ph >= 18) {  // room for a second line
                        gfx->text("s" + std::to_string(seq), Point(x0 + 2, 11), pw, 1.0f);
                    }
                }
            }
        }

        // Dimensions self-test, layout-aware: a border around the *logical* display
        // (mapped across panels) + the display size, anchored inside panel (0,0).
        void render_dims_logical() {
            const int dw = geo.display_w, dh = geo.display_h;
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            gfx->set_pen(255, 255, 255);
            for (int lx = 0; lx < dw; lx++) { plot_logical(lx, 0); plot_logical(lx, dh - 1); }
            for (int ly = 0; ly < dh; ly++) { plot_logical(0, ly); plot_logical(dw - 1, ly); }
            gfx->set_font("bitmap8");
            int ty = geo.panel_h / 2 - 4; if (ty < 2) ty = 2;
            int cx, cy;
            map_to_chain(3, ty, cx, cy);  // top-left of panel (0,0); stays within it
            gfx->text(std::to_string(dw) + "x" + std::to_string(dh), Point(cx, cy), geo.panel_w, 1.0f);
        }

        void fill_logical_rect(int lx, int ly, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
            gfx->set_pen(r, g, b);
            for (int yy = 0; yy < h; yy++)
                for (int xx = 0; xx < w; xx++) plot_logical(lx + xx, ly + yy);
        }

        // Test 30 (border + diagonal) across the logical display.
        void render_geometry_logical() {
            const int dw = geo.display_w, dh = geo.display_h;
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            gfx->set_pen(255, 255, 255);
            for (int lx = 0; lx < dw; lx++) { plot_logical(lx, 0); plot_logical(lx, dh - 1); }
            for (int ly = 0; ly < dh; ly++) { plot_logical(0, ly); plot_logical(dw - 1, ly); }
            int n = dw < dh ? dw : dh;
            for (int i = 0; i < n; i++) plot_logical(i, i);
        }

        // Test 31 (corner markers) at the logical display's corners.
        void render_corners_logical() {
            const int dw = geo.display_w, dh = geo.display_h;
            int s = (dw < dh ? dw : dh) / 8; if (s < 1) s = 1;
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            fill_logical_rect(0,      0,      s, s, 255,   0,   0);  // TL red
            fill_logical_rect(dw - s, 0,      s, s,   0, 255,   0);  // TR green
            fill_logical_rect(0,      dh - s, s, s,   0,   0, 255);  // BL blue
            fill_logical_rect(dw - s, dh - s, s, s, 255, 255, 255);  // BR white
        }

        // Test 50 (numbered 16×16 tiles) in logical reading order across the
        // display; each tile aligns to a panel, so it maps to a contiguous
        // chain rectangle.
        void render_grid_logical() {
            const int cell = 16;
            int cols = geo.display_w / cell;
            int rows = geo.display_h / cell;
            int total = cols * rows; if (total < 1) total = 1;
            gfx->set_pen(0, 0, 0);
            gfx->clear();
            gfx->set_font("bitmap8");
            int idx = 0;
            for (int ry = 0; ry < rows; ry++) {
                for (int cxk = 0; cxk < cols; cxk++) {
                    RGB bg = RGB::from_hsv((float)idx / (float)total, 1.0f, 1.0f);
                    int cx, cy;
                    map_to_chain(cxk * cell, ry * cell, cx, cy);  // tile origin
                    gfx->set_pen(bg.r, bg.g, bg.b);
                    gfx->rectangle(Rect(cx, cy, cell, cell));
                    if (bg.luminance() > 12750) gfx->set_pen(0, 0, 0);
                    else gfx->set_pen(255, 255, 255);
                    gfx->text(std::to_string(idx), Point(cx + 2, cy + 4), cell, 1.0f);
                    idx++;
                }
            }
        }
    }

    int      width()       { return geo.display_w; }
    int      height()      { return geo.display_h; }
    size_t   buffer_size() { return static_cast<size_t>(geo.chain_w) * geo.chain_h * 4; }
    uint8_t* back()        { return buf[1 - front]; }
    const Geometry& geometry() { return geo; }

    void __isr dma_complete() {
        panel->dma_complete();
    }

    void init() {
        bool bad_cfg = load_geometry();  // needs config_boot() to have run (see main.cpp)

        // Two framebuffers at the flat chain geometry (E6): front is shown, back is
        // written. The back allocation may fail on RP2040 near max dims → single-
        // buffered fallback. The logical layout is applied when rendering self-tests
        // (E11 S11.2) and by the host for streamed frames.
        size_t bytes = buffer_size();
        buf[0] = new uint8_t[bytes];
        pg[0]  = new PicoGraphics_PenRGB888(geo.chain_w, geo.chain_h, buf[0]);
        buf[1] = new (std::nothrow) uint8_t[bytes];
        if (buf[1] != nullptr) {
            pg[1] = new PicoGraphics_PenRGB888(geo.chain_w, geo.chain_h, buf[1]);
            dbuf  = true;
        } else {
            buf[1] = buf[0]; pg[1] = pg[0]; dbuf = false;  // single-buffered
        }
        front = 0;
        gfx   = pg[1 - front];  // draw target = back

        panel = new Hub75(geo.chain_w, geo.chain_h, nullptr, PANEL_GENERIC, false, parse_order());
        panel->start(dma_complete);

        if (bad_cfg) {
            info("cfg: bad geometry");  // diagnostic, then usable at the fallback size
        } else if (!dbuf) {
            // Back buffer didn't fit (RP2040 near max dims): running single-buffered,
            // so `hold`/`flip` can't truly defer. Flag it on the boot screen.
            info(std::string("1buf ") + MULTIVERSE_VERSION);
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
        if (dbuf) front ^= 1;      // show the freshly-drawn back buffer
        panel->update(pg[front]);
        gfx = pg[1 - front];       // draw target follows to the now-hidden buffer
    }

    void selftest(uint8_t test_id) {
        // 41 = the real info() screen. 60 = the layout test (always board-rendered).
        // 42 = dimensions: layout-aware when there's more than one panel, else the
        // generic flat pattern. Everything else is a flat-chain pattern (for a 1×1
        // panel that's identical to the logical render).
        if (test_id == display_selftest::INFO_SCREEN) {
            info(MULTIVERSE_VERSION);
            return;
        }
        // Display-level tests (dimensions, geometry, corners, grid) render in
        // logical space when there's more than one panel; panel-level tests
        // (fills, rows, columns) stay on the flat chain to expose each panel's
        // own wiring. For a 1×1 panel the mapping is identity, so both paths match.
        bool multi = geo.panels_x * geo.panels_y > 1;
        if (test_id == display_selftest::LAYOUT_SCREEN) {
            render_layout();
        } else if (multi && test_id == 42) {
            render_dims_logical();
        } else if (multi && test_id == 30) {
            render_geometry_logical();
        } else if (multi && test_id == 31) {
            render_corners_logical();
        } else if (multi && test_id == 50) {
            render_grid_logical();
        } else {
            display_selftest::render(*gfx, geo.chain_w, geo.chain_h, test_id);
        }
        update();
    }
}
