// Host unit test for the log-structured flash persistence logic (E2 S2.3).
// Pure host build with a RAM-backed FlashOps that models NOR semantics.
//   c++ -std=c++17 -Wall -Wextra -Isrc test/kv_log_test.cpp \
//       src/config/kv_log.cpp src/config/kv_store.cpp -o kv_log_test && ./kv_log_test
#include "config/kv_log.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

// Standard CRC-32 (IEEE) — self-consistent stamp/validate for the test.
static uint32_t test_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1) + 1));
    }
    return ~crc;
}

// RAM flash: erased = 0xFF; program only clears bits (dst &= src), like NOR.
struct RamFlashOps : kv::FlashOps {
    std::vector<uint8_t> buf;
    explicit RamFlashOps(size_t sz) : buf(sz, 0xFF) {}
    size_t size() const override { return buf.size(); }
    const uint8_t* read(size_t off) const override { return buf.data() + off; }
    void erase(size_t off, size_t len) override { memset(buf.data() + off, 0xFF, len); }
    void program(size_t off, const uint8_t* data, size_t len) override {
        for (size_t i = 0; i < len; i++) buf[off + i] &= data[i];
    }
};

static const uint8_t* B(const char* s) { return reinterpret_cast<const uint8_t*>(s); }
static bool val_is(kv::Store& s, const char* key, const char* expect) {
    size_t len = 0;
    const uint8_t* v = s.get(key, len);
    return v && len == strlen(expect) && memcmp(v, expect, len) == 0;
}

static constexpr size_t REGION = 8 * 1024;  // 2 sectors = 32 pages (header + 31 records)

int main() {
    // --- format on blank region, then append/reload survives ---
    {
        RamFlashOps ops(REGION);
        kv::Store s;
        kv::Log log(ops, test_crc32);
        log.load(s);                 // blank -> format
        CHECK(s.count() == 0);

        CHECK(log.put(s, "board", B("i75"), 3));
        CHECK(log.put(s, "width", B("256"), 3));
        CHECK(val_is(s, "board", "i75"));

        kv::Store s2;                // "reboot": fresh store, same flash
        kv::Log log2(ops, test_crc32);
        log2.load(s2);
        CHECK(s2.count() == 2);
        CHECK(val_is(s2, "board", "i75"));
        CHECK(val_is(s2, "width", "256"));
    }

    // --- update supersedes by seq; del tombstone hides key ---
    {
        RamFlashOps ops(REGION);
        kv::Store s; kv::Log log(ops, test_crc32); log.load(s);
        CHECK(log.put(s, "k", B("aaa"), 3));
        CHECK(log.put(s, "k", B("bbb"), 3));   // update
        CHECK(log.put(s, "gone", B("x"), 1));
        CHECK(log.del(s, "gone"));

        kv::Store s2; kv::Log log2(ops, test_crc32); log2.load(s2);
        CHECK(val_is(s2, "k", "bbb"));         // latest wins
        size_t len = 0;
        CHECK(s2.get("gone", len) == nullptr); // tombstoned
        CHECK(s2.count() == 1);
    }

    // --- power loss: a torn append fails CRC; the prior value stays authoritative ---
    {
        RamFlashOps ops(REGION);
        kv::Store s; kv::Log log(ops, test_crc32); log.load(s);
        CHECK(log.put(s, "k", B("v1"), 2));    // committed at page 1; page 2 is next free

        // Build a would-be "k"="v2" record with a higher seq, then program only
        // the first 64 bytes of its page — a write interrupted by power loss.
        kv::Record torn; memset(&torn, 0, sizeof(torn));
        torn.key_len = kv::Store::normalize_key("k", torn.key);
        memcpy(torn.value, "v2", 2); torn.value_len = 2;
        torn.seq = 999;  // would win if it were valid
        torn.crc32 = test_crc32(reinterpret_cast<const uint8_t*>(&torn), kv::CRC_COVERAGE);
        ops.program(2 * kv::PAGE_SIZE, reinterpret_cast<const uint8_t*>(&torn), 64);

        kv::Store s2; kv::Log log2(ops, test_crc32);
        log2.load(s2);                         // must not crash
        CHECK(val_is(s2, "k", "v1"));          // torn v2 ignored, v1 authoritative
        CHECK(s2.count() == 1);

        // A subsequent put still works (append point moved past the torn page).
        CHECK(log2.put(s2, "k", B("v3"), 2));
        kv::Store s3; kv::Log log3(ops, test_crc32); log3.load(s3);
        CHECK(val_is(s3, "k", "v3"));
    }

    // --- log fills -> compaction keeps the live set, no error ---
    {
        RamFlashOps ops(REGION);
        kv::Store s; kv::Log log(ops, test_crc32); log.load(s);
        // 31 record slots; 60 updates to one key force at least one compaction.
        for (int i = 0; i < 60; i++) {
            char v[8]; snprintf(v, sizeof(v), "n%d", i);
            CHECK(log.put(s, "ctr", B(v), strlen(v)));
        }
        CHECK(val_is(s, "ctr", "n59"));

        kv::Store s2; kv::Log log2(ops, test_crc32); log2.load(s2);
        CHECK(s2.count() == 1);
        CHECK(val_is(s2, "ctr", "n59"));       // survived compaction + reload
    }

    if (failures == 0) printf("kv_log_test: all checks passed\n");
    else printf("kv_log_test: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
