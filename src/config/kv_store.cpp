#include "config/kv_store.hpp"

#include <cstring>

namespace kv {

uint8_t Store::normalize_key(std::string_view key, char out[KEY_LEN]) {
    size_t n = key.size() < KEY_LEN ? key.size() : KEY_LEN;  // truncate if longer
    for (size_t i = 0; i < KEY_LEN; i++) {
        out[i] = (i < n) ? key[i] : ' ';                     // space-pad the tail
    }
    return static_cast<uint8_t>(n);
}

int Store::find(const char key[KEY_LEN]) const {
    for (size_t i = 0; i < count_; i++) {
        if (memcmp(records_[i].key, key, KEY_LEN) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Store::put(std::string_view key, const uint8_t* value, size_t value_len) {
    if (value_len > VALUE_LEN) {
        return false;  // caller must not exceed the value width
    }

    char norm[KEY_LEN];
    uint8_t key_len = normalize_key(key, norm);

    int idx = find(norm);
    if (idx < 0) {
        if (count_ >= CAPACITY) {
            return false;  // new key, no room
        }
        idx = static_cast<int>(count_++);
    }

    Record& r = records_[idx];
    memcpy(r.key, norm, KEY_LEN);
    r.key_len = key_len;
    memcpy(r.value, value, value_len);
    memset(r.value + value_len, 0, VALUE_LEN - value_len);  // zero the tail
    r.value_len = static_cast<uint8_t>(value_len);
    r.seq = 0;    // assigned by the flash layer (S2.3)
    r.crc32 = 0;  // computed by the flash layer (S2.3)
    return true;
}

const uint8_t* Store::get(std::string_view key, size_t& value_len_out) const {
    char norm[KEY_LEN];
    normalize_key(key, norm);

    int idx = find(norm);
    if (idx < 0) {
        return nullptr;
    }
    value_len_out = records_[idx].value_len;
    return records_[idx].value;
}

bool Store::del(std::string_view key) {
    char norm[KEY_LEN];
    normalize_key(key, norm);

    int idx = find(norm);
    if (idx < 0) {
        return false;
    }
    // Compact by moving the last live record into the freed slot.
    size_t last = count_ - 1;
    if (static_cast<size_t>(idx) != last) {
        records_[idx] = records_[last];
    }
    count_--;
    return true;
}

} // namespace kv
