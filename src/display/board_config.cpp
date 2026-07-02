#include "display/board_config.hpp"

namespace display {

namespace {

// Canonical per-board facts — the single home for what the four <board>.cpp
// files used to hard-code individually.
BoardDescriptor defaults_for(BoardKind kind) {
    switch (kind) {
        case BoardKind::Galactic: return {BoardKind::Galactic, 53, 11, "bitmap5", true,  false};
        case BoardKind::Cosmic:   return {BoardKind::Cosmic,   32, 32, "bitmap5", true,  false};
        case BoardKind::Stellar:  return {BoardKind::Stellar,  16, 16, "bitmap5", true,  false};
        case BoardKind::I75:      return {BoardKind::I75,     256, 64, "bitmap8", false, false};
        case BoardKind::Plasma:   return {BoardKind::Plasma,    1, 64, "bitmap8", false, false};
        case BoardKind::Unknown:
        default:
            // No known board: fall back to a safe, large-enough surface and flag it.
            return {BoardKind::Unknown, 256, 64, "bitmap8", false, true};
    }
}

// Parse an ASCII-decimal k/v value. Returns -1 if empty or non-numeric.
long parse_uint(const uint8_t* v, size_t len) {
    if (v == nullptr || len == 0) return -1;
    long n = 0;
    for (size_t i = 0; i < len; i++) {
        if (v[i] < '0' || v[i] > '9') return -1;
        n = n * 10 + (v[i] - '0');
        if (n > 100000) return -1;  // absurd; bail before overflow
    }
    return n;
}

// Read a dimension key (ASCII decimal), returning `def` when absent or invalid.
// Basic 1..256 sanity only — S3.5 tightens to per-axis base-2 ranges.
int read_dim(kv::Store& store, std::string_view key, int def) {
    size_t vlen = 0;
    const uint8_t* v = store.get(key, vlen);
    long n = parse_uint(v, vlen);
    if (n < 1 || n > 256) return def;
    return static_cast<int>(n);
}

} // namespace

BoardKind parse_board_kind(std::string_view name) {
    if (name == "galactic") return BoardKind::Galactic;
    if (name == "cosmic")   return BoardKind::Cosmic;
    if (name == "stellar")  return BoardKind::Stellar;
    if (name == "i75" || name == "i75w") return BoardKind::I75;
    if (name == "plasma")   return BoardKind::Plasma;
    return BoardKind::Unknown;
}

BoardDescriptor describe(kv::Store& store, BoardKind fallback) {
    BoardKind kind = fallback;
    bool invalid = false;

    size_t vlen = 0;
    const uint8_t* bv = store.get("board", vlen);
    if (bv) {
        BoardKind k = parse_board_kind(std::string_view(reinterpret_cast<const char*>(bv), vlen));
        if (k == BoardKind::Unknown) {
            invalid = true;      // unparseable board value — keep the fallback
        } else {
            kind = k;
        }
    }

    BoardDescriptor d = defaults_for(kind);
    d.invalid = d.invalid || invalid;

    // Configurable dimensions: i75 (width+height) and Plasma (length = height).
    if (kind == BoardKind::I75) {
        d.width  = read_dim(store, "width",  d.width);
        d.height = read_dim(store, "height", d.height);
    } else if (kind == BoardKind::Plasma) {
        d.height = read_dim(store, "height", d.height);  // strip length
    }

    return d;
}

} // namespace display
