#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "config/kv_format.hpp"

// In-RAM key/value store (E2 S2.2).
//
// Holds the materialised *live view* of the configuration: at most one entry per
// key, kept compacted in RAM. This is what the rest of the firmware reads/writes;
// it performs no flash I/O. S2.3 loads records from flash into this view and
// appends changes back, assigning `seq` and computing the per-record CRC — the
// store itself leaves Record::seq / Record::crc32 zero.
namespace kv {

class Store {
public:
    // Live-view capacity. Config uses only a handful of keys; tunable.
    static constexpr size_t CAPACITY = 32;

    // Normalise a key into the on-flash 8-byte form: up to KEY_LEN bytes copied,
    // the remainder space-padded, longer keys truncated. Returns the significant
    // length (1..KEY_LEN). The single place key shaping happens.
    static uint8_t normalize_key(std::string_view key, char out[KEY_LEN]);

    // Insert or update `key`. Returns false if value_len > VALUE_LEN, or if the
    // key is new and the store is full. Bytes past value_len are zeroed.
    bool put(std::string_view key, const uint8_t* value, size_t value_len);

    // Look up `key`. On hit returns a pointer to the internal VALUE_LEN-byte value
    // and sets value_len_out to the significant length; returns nullptr on miss.
    const uint8_t* get(std::string_view key, size_t& value_len_out) const;

    // Remove `key`. Returns true if it was present.
    bool del(std::string_view key);

    // Iteration over live records (e.g. for S2.3 persistence or a config dump).
    size_t count() const { return count_; }
    const Record& at(size_t i) const { return records_[i]; }

    void clear() { count_ = 0; }

private:
    // Index of the record whose key matches the normalised `key`, or -1.
    int find(const char key[KEY_LEN]) const;

    Record records_[CAPACITY] = {};
    size_t count_ = 0;
};

} // namespace kv
