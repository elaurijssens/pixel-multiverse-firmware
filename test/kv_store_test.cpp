// Host unit test for the in-RAM k/v store (E2 S2.2). Pure host build — no Pico
// SDK. Compile & run:
//   c++ -std=c++17 -Wall -Wextra -Isrc test/kv_store_test.cpp src/config/kv_store.cpp -o kv_store_test && ./kv_store_test
#include "config/kv_store.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static bool value_is(const uint8_t* v, size_t len, const char* expect) {
    return v && len == strlen(expect) && memcmp(v, expect, len) == 0;
}

static const uint8_t* bytes(const char* s) { return reinterpret_cast<const uint8_t*>(s); }

int main() {
    // --- put/get round-trip + update-in-place ---
    {
        kv::Store s;
        CHECK(s.put("board", bytes("i75"), 3));
        size_t len = 0;
        const uint8_t* v = s.get("board", len);
        CHECK(value_is(v, len, "i75"));

        CHECK(s.put("board", bytes("galactic"), 8));  // update, not insert
        v = s.get("board", len);
        CHECK(value_is(v, len, "galactic"));
        CHECK(s.count() == 1);  // still one entry
    }

    // --- miss returns nullptr; del removes ---
    {
        kv::Store s;
        size_t len = 0;
        CHECK(s.get("nope", len) == nullptr);
        CHECK(!s.del("nope"));

        const uint8_t raw[2] = {0x00, 0x01};  // value may contain arbitrary bytes
        CHECK(s.put("width", raw, 2));
        CHECK(s.del("width"));
        CHECK(s.get("width", len) == nullptr);
        CHECK(s.count() == 0);
    }

    // --- key normalisation: space-pad short, truncate >8, both forms match ---
    {
        kv::Store s;
        char norm[kv::KEY_LEN];
        CHECK(kv::Store::normalize_key("abc", norm) == 3);
        CHECK(memcmp(norm, "abc     ", kv::KEY_LEN) == 0);           // space-padded
        CHECK(kv::Store::normalize_key("abcdefghij", norm) == 8);   // truncated length
        CHECK(memcmp(norm, "abcdefgh", kv::KEY_LEN) == 0);

        size_t len = 0;
        CHECK(s.put("hi", bytes("x"), 1));
        CHECK(s.get("hi", len) != nullptr);          // padded lookup
        CHECK(s.get("hi      ", len) != nullptr);    // explicit 8-char padded form hits same record
        CHECK(s.count() == 1);

        CHECK(s.put("abcdefghij", bytes("y"), 1));   // stored under "abcdefgh"
        CHECK(s.get("abcdefgh", len) != nullptr);
        CHECK(s.get("abcdefghZZ", len) != nullptr);  // also truncates to "abcdefgh" -> same record
        CHECK(s.count() == 2);
    }

    // --- value tail zeroed past value_len ---
    {
        kv::Store s;
        CHECK(s.put("k", bytes("abc"), 3));
        const kv::Record& r = s.at(0);
        CHECK(r.value_len == 3);
        bool tail_zero = true;
        for (size_t i = 3; i < kv::VALUE_LEN; i++) tail_zero &= (r.value[i] == 0);
        CHECK(tail_zero);
        CHECK(r.seq == 0 && r.crc32 == 0);  // flash-owned, left zero by S2.2
    }

    // --- reject over-long value ---
    {
        kv::Store s;
        uint8_t big[kv::VALUE_LEN + 1] = {0};
        CHECK(!s.put("k", big, sizeof(big)));
        CHECK(s.put("k", big, kv::VALUE_LEN));  // exactly VALUE_LEN is fine
    }

    // --- capacity: fill, reject new key, still allow updates ---
    {
        kv::Store s;
        for (size_t i = 0; i < kv::Store::CAPACITY; i++) {
            char key[8];
            snprintf(key, sizeof(key), "k%02zu", i);
            CHECK(s.put(key, bytes("v"), 1));
        }
        CHECK(s.count() == kv::Store::CAPACITY);
        CHECK(!s.put("overflow", bytes("v"), 1));  // new key, full
        CHECK(s.put("k00", bytes("w"), 1));         // update existing still works
        CHECK(s.count() == kv::Store::CAPACITY);
    }

    if (failures == 0) printf("kv_store_test: all checks passed\n");
    else printf("kv_store_test: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
