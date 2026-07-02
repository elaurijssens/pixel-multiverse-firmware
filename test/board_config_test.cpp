// Host unit test for the board descriptor (E3 S3.1). Pure host build — no SDK.
//   c++ -std=c++17 -Wall -Wextra -Isrc test/board_config_test.cpp \
//       src/display/board_config.cpp src/config/kv_store.cpp -o board_config_test && ./board_config_test
#include "display/board_config.hpp"

#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static const uint8_t* B(const char* s) { return reinterpret_cast<const uint8_t*>(s); }
static void set(kv::Store& s, const char* k, const char* v) {
    CHECK(s.put(k, B(v), strlen(v)));
}

int main() {
    using display::BoardKind;

    // --- absent board -> fallback + that board's table defaults ---
    {
        kv::Store s;
        auto d = display::describe(s, BoardKind::I75);
        CHECK(d.board == BoardKind::I75);
        CHECK(d.width == 256 && d.height == 64);
        CHECK(strcmp(d.font, "bitmap8") == 0);
        CHECK(!d.has_audio && !d.invalid);

        kv::Store s2;
        auto g = display::describe(s2, BoardKind::Galactic);
        CHECK(g.board == BoardKind::Galactic);
        CHECK(g.width == 53 && g.height == 11);
        CHECK(strcmp(g.font, "bitmap5") == 0);
        CHECK(g.has_audio && !g.invalid);
    }

    // --- board key overrides the fallback ---
    {
        kv::Store s;
        set(s, "board", "cosmic");
        auto d = display::describe(s, BoardKind::I75);  // fallback ignored
        CHECK(d.board == BoardKind::Cosmic);
        CHECK(d.width == 32 && d.height == 32);
        CHECK(strcmp(d.font, "bitmap5") == 0);
        CHECK(d.has_audio && !d.invalid);
    }

    // --- i75 reads width/height from the store ---
    {
        kv::Store s;
        set(s, "board", "i75");
        set(s, "width", "128");
        set(s, "height", "32");
        auto d = display::describe(s, BoardKind::I75);
        CHECK(d.board == BoardKind::I75);
        CHECK(d.width == 128 && d.height == 32);
        CHECK(!d.invalid);
        // i75w parses to the same kind
        CHECK(display::parse_board_kind("i75w") == BoardKind::I75);
    }

    // --- unicorn ignores width/height (fixed dims) ---
    {
        kv::Store s;
        set(s, "board", "stellar");
        set(s, "width", "128");
        set(s, "height", "128");
        auto d = display::describe(s, BoardKind::I75);
        CHECK(d.board == BoardKind::Stellar);
        CHECK(d.width == 16 && d.height == 16);  // unchanged
    }

    // --- invalid board value -> keep fallback, flag invalid, no crash ---
    {
        kv::Store s;
        set(s, "board", "frobnicator");
        auto d = display::describe(s, BoardKind::Cosmic);
        CHECK(d.board == BoardKind::Cosmic);   // fallback retained
        CHECK(d.width == 32 && d.height == 32);
        CHECK(d.invalid);                      // flagged for a diagnostic
    }

    // --- unknown fallback -> safe default surface, flagged ---
    {
        kv::Store s;
        auto d = display::describe(s, BoardKind::Unknown);
        CHECK(d.board == BoardKind::Unknown);
        CHECK(d.width == 256 && d.height == 64);
        CHECK(d.invalid);
    }

    // --- garbage dimension value falls back to the default ---
    {
        kv::Store s;
        set(s, "board", "i75");
        set(s, "width", "12x");   // non-numeric
        auto d = display::describe(s, BoardKind::I75);
        CHECK(d.width == 256);    // default kept
    }

    if (failures == 0) printf("board_config_test: all checks passed\n");
    else printf("board_config_test: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
