#pragma once

#include <cstdint>
#include <string>
#include "libraries/pico_graphics/pico_graphics.hpp"

// Board-agnostic display self-test patterns (bench diagnostic).
//
// render() draws the diagnostic pattern selected by a two-digit id into the
// given PicoGraphics surface; the caller flushes it to the panel with its own
// update(). Patterns are grouped by the tens digit so more can be added later
// without disturbing existing ids:
//
//   0x  solid fills      — stuck/swapped channels, brightness
//   1x  row patterns     — out-of-order / interleaved rows
//   2x  column patterns  — out-of-order / interleaved columns
//   3x  geometry         — orientation, mirroring, edges
//   4x  text / colour    — colour rendering + font legibility
//   5x  grid / addressing — per-tile numbering to map panel wiring
//   6x  panel layout      — multi-panel geometry (i75, board-rendered)
//   7x  strip patterns     — linear-strip tests (plasma, board-rendered):
//                            70 spectrum, 71 decade markers, 72 endpoints
//
// Every pattern is drawn through set_pen(r, g, b) rather than by packing bytes
// into the framebuffer, so the test exercises the *panel wiring*, not our pixel
// packing: a red fill that shows up green means the panel swaps channels, not
// that the buffer layout is wrong.
namespace display_selftest {

// Test 41 renders the board's real info() screen rather than a buffer pattern,
// so it is handled by the per-board selftest() wrapper (info() is board-specific
// — font and layout differ per board) instead of by render() below.
inline constexpr uint8_t INFO_SCREEN = 41;

// Test 60 renders the panel-layout map (each panel shaded + bordered + labelled
// with its col,row and chain sequence). Like INFO_SCREEN it needs the board's
// runtime geometry, so it is handled by the per-board selftest() wrapper, not
// render() below.
//
// On a multi-panel i75 the wrapper also renders the *display-level* patterns in
// logical space — dimensions (42), geometry (30), corners (31) and grid (50) —
// so they describe the assembled display, not the flat chain. The *panel-level*
// patterns (fills 0x, rows 1x, columns 2x) stay on the flat chain so each
// physical panel's own wiring is still exercised. For a 1×1 panel the logical
// mapping is identity, so every pattern here renders exactly as drawn.
inline constexpr uint8_t LAYOUT_SCREEN = 60;

// Fill an inclusive column span of the whole height with the current pen.
inline void fill_column(pimoroni::PicoGraphics& g, int x, int height) {
    for (int y = 0; y < height; y++) g.pixel(pimoroni::Point(x, y));
}

// Fill a whole row with the current pen.
inline void fill_row(pimoroni::PicoGraphics& g, int y, int width) {
    for (int x = 0; x < width; x++) g.pixel(pimoroni::Point(x, y));
}

inline void render(pimoroni::PicoGraphics& g, int width, int height, uint8_t id) {
    using pimoroni::Point;
    using pimoroni::Rect;
    using pimoroni::RGB;

    // Every pattern starts from a cleared (black) surface.
    g.set_pen(0, 0, 0);
    g.clear();

    switch (id) {
        // ---- 0x: solid fills (channel & brightness sanity) -------------
        case 0:                                          break;  // all off
        case 1: g.set_pen(255, 255, 255); g.clear();     break;  // white
        case 2: g.set_pen(255,   0,   0); g.clear();     break;  // red
        case 3: g.set_pen(  0, 255,   0); g.clear();     break;  // green
        case 4: g.set_pen(  0,   0, 255); g.clear();     break;  // blue
        case 5: g.set_pen(128, 128, 128); g.clear();     break;  // 50% grey

        // ---- 1x: row order (interleaved / reordered rows) --------------
        case 10:  // vertical brightness ramp: row 0 dark → last row bright
            for (int y = 0; y < height; y++) {
                uint8_t v = height > 1 ? (uint8_t)(y * 255 / (height - 1)) : 255;
                g.set_pen(v, v, v);
                fill_row(g, y, width);
            }
            break;
        case 11:  // row parity: even rows red, odd rows green
            for (int y = 0; y < height; y++) {
                if (y & 1) g.set_pen(0, 255, 0); else g.set_pen(255, 0, 0);
                fill_row(g, y, width);
            }
            break;
        case 12:  // top half red, bottom half blue
            for (int y = 0; y < height; y++) {
                if (y < height / 2) g.set_pen(255, 0, 0); else g.set_pen(0, 0, 255);
                fill_row(g, y, width);
            }
            break;

        // ---- 2x: column order (interleaved / reordered columns) --------
        case 20:  // horizontal brightness ramp: col 0 dark → last col bright
            for (int x = 0; x < width; x++) {
                uint8_t v = width > 1 ? (uint8_t)(x * 255 / (width - 1)) : 255;
                g.set_pen(v, v, v);
                fill_column(g, x, height);
            }
            break;
        case 21:  // column parity: even columns red, odd columns green
            for (int x = 0; x < width; x++) {
                if (x & 1) g.set_pen(0, 255, 0); else g.set_pen(255, 0, 0);
                fill_column(g, x, height);
            }
            break;
        case 22:  // left half red, right half blue
            for (int x = 0; x < width; x++) {
                if (x < width / 2) g.set_pen(255, 0, 0); else g.set_pen(0, 0, 255);
                fill_column(g, x, height);
            }
            break;

        // ---- 3x: geometry / orientation --------------------------------
        case 30: {  // 1px white border + main diagonal
            g.set_pen(255, 255, 255);
            fill_row(g, 0, width);
            fill_row(g, height - 1, width);
            fill_column(g, 0, height);
            fill_column(g, width - 1, height);
            int n = width < height ? width : height;
            for (int i = 0; i < n; i++) g.pixel(Point(i, i));
            break;
        }
        case 31: {  // corner markers: TL red, TR green, BL blue, BR white
            int s = width < height ? width : height;
            s = s / 8; if (s < 1) s = 1;      // block scales with the panel
            g.set_pen(255,   0,   0); g.rectangle(Rect(0,             0,              s, s));
            g.set_pen(  0, 255,   0); g.rectangle(Rect(width - s,     0,              s, s));
            g.set_pen(  0,   0, 255); g.rectangle(Rect(0,             height - s,     s, s));
            g.set_pen(255, 255, 255); g.rectangle(Rect(width - s,     height - s,     s, s));
            break;
        }

        // ---- 4x: text / colour rendering -------------------------------
        case 40: {  // colour-name words, each drawn in its own colour
            struct Named { const char* name; uint8_t r, g, b; };
            static const Named words[] = {
                {"red",     255,   0,   0},
                {"green",     0, 255,   0},
                {"blue",      0,   0, 255},
                {"yellow",  255, 255,   0},
                {"cyan",      0, 255, 255},
                {"magenta", 255,   0, 255},
                {"white",   255, 255, 255},
                {"grey",    128, 128, 128},
            };
            const int count = 8;
            int line_h = height / count;   // 8 px on a 64-high panel
            if (line_h < 8) line_h = 8;    // don't overlap on shorter panels
            g.set_font("bitmap8");
            for (int i = 0; i < count; i++) {
                g.set_pen(words[i].r, words[i].g, words[i].b);
                g.text(words[i].name, Point(1, i * line_h), width, 1.0f);
            }
            break;
        }
        // 41 (INFO_SCREEN) is intentionally not drawn here — the board's
        // selftest() wrapper renders the real info() screen for it.
        case 42: {  // dimensions check: 1px white border + "WxH" text, to eyeball
                    // a freshly-configured panel size (edges flush, text reads right)
            g.set_pen(255, 255, 255);
            fill_row(g, 0, width);
            fill_row(g, height - 1, width);
            fill_column(g, 0, height);
            fill_column(g, width - 1, height);
            g.set_font("bitmap8");
            std::string dims = std::to_string(width) + "x" + std::to_string(height);
            int ty = height / 2 - 4; if (ty < 2) ty = 2;   // vertically centre the 8px glyphs
            g.text(dims, Point(3, ty), width, 1.0f);
            break;
        }

        // ---- 5x: grid / addressing -------------------------------------
        case 50: {  // number each 16x16 tile; distinct hue + contrasting text
            const int cell = 16;
            int cols = width / cell;
            int rows = height / cell;
            int total = cols * rows;
            if (total < 1) total = 1;
            g.set_font("bitmap8");
            int idx = 0;
            for (int ry = 0; ry < rows; ry++) {
                for (int cx = 0; cx < cols; cx++) {
                    RGB bg = RGB::from_hsv((float)idx / (float)total, 1.0f, 1.0f);
                    int px = cx * cell, py = ry * cell;
                    g.set_pen(bg.r, bg.g, bg.b);
                    g.rectangle(Rect(px, py, cell, cell));
                    // Contrasting text: black on light tiles, white on dark.
                    // luminance() maxes at 255*100; 12750 is the midpoint.
                    if (bg.luminance() > 12750) g.set_pen(0, 0, 0);
                    else g.set_pen(255, 255, 255);
                    g.text(std::to_string(idx), Point(px + 2, py + 4), cell, 1.0f);
                    idx++;
                }
            }
            break;
        }

        default:
            // Unknown id: leave the panel black so an unhandled test is
            // visibly distinct from any real pattern.
            break;
    }
}

} // namespace display_selftest
