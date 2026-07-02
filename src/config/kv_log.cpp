#include "config/kv_log.hpp"

#include <cstring>

namespace kv {

namespace {
std::string_view key_view(const Record& r) {
    size_t n = r.key_len > KEY_LEN ? KEY_LEN : r.key_len;
    return std::string_view(r.key, n);
}
}  // namespace

bool Log::header_valid() const {
    const Header& h = header();
    return memcmp(h.magic, MAGIC, sizeof(MAGIC)) == 0 &&
           h.version == FORMAT_VERSION &&
           h.record_size == RECORD_SIZE &&
           crc_(reinterpret_cast<const uint8_t*>(&h), CRC_COVERAGE) == h.crc32;
}

bool Log::record_valid(const Record& r) const {
    if (static_cast<uint8_t>(r.key[0]) == SLOT_ERASED) return false;
    return crc_(reinterpret_cast<const uint8_t*>(&r), CRC_COVERAGE) == r.crc32;
}

bool Log::page_erased(size_t page) const {
    const uint8_t* p = ops_.read(page * PAGE_SIZE);
    for (size_t i = 0; i < PAGE_SIZE; i++) {
        if (p[i] != 0xFF) return false;
    }
    return true;
}

void Log::write_slot(size_t page, const void* rec128) {
    uint8_t buf[PAGE_SIZE];
    memset(buf, 0xFF, PAGE_SIZE);              // unused tail stays erased
    memcpy(buf, rec128, RECORD_SIZE);
    ops_.program(page * PAGE_SIZE, buf, PAGE_SIZE);
}

void Log::fill_record(Record& r, std::string_view key, const uint8_t* value,
                      size_t value_len, uint8_t flags) const {
    memset(&r, 0, sizeof(r));
    r.key_len = Store::normalize_key(key, r.key);
    if (value && value_len) memcpy(r.value, value, value_len);
    r.value_len = static_cast<uint8_t>(value_len);
    r.flags = flags;
    // seq / crc32 are stamped in stamp_and_append().
}

void Log::stamp_and_append(Record& r, Store& store) {
    if (append_page_ >= page_count()) {
        // Log full: compaction rewrites the whole live set (which already
        // reflects this change, applied to `store` by the caller), so the
        // record `r` is persisted by compact() itself.
        compact(store);
        return;
    }
    r.seq = next_seq_++;
    r.crc32 = crc_(reinterpret_cast<const uint8_t*>(&r), CRC_COVERAGE);
    write_slot(append_page_, &r);
    append_page_++;
}

void Log::compact(Store& store) {
    ops_.erase(0, ops_.size());

    Header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, MAGIC, sizeof(MAGIC));
    h.version = FORMAT_VERSION;
    h.record_size = RECORD_SIZE;
    h.crc32 = crc_(reinterpret_cast<const uint8_t*>(&h), CRC_COVERAGE);
    write_slot(0, &h);

    next_seq_ = 1;  // fresh region — restart; page order still == seq order
    size_t page = 1;
    for (size_t i = 0; i < store.count() && page < page_count(); i++) {
        Record r = store.at(i);  // key/value/lengths; seq/crc/flags reset below
        r.flags = 0;
        r.seq = next_seq_++;
        r.crc32 = crc_(reinterpret_cast<const uint8_t*>(&r), CRC_COVERAGE);
        write_slot(page++, &r);
    }
    append_page_ = page;
}

void Log::format(Store& store) {
    store.clear();
    compact(store);  // erases, writes header, no records
}

void Log::load(Store& store) {
    store.clear();
    if (!header_valid()) {
        format(store);
        return;
    }

    uint32_t max_seq = 0;
    size_t first_free = 0;
    // Pages are appended in ascending seq order, so replaying valid records in
    // page order gives highest-seq-per-key; tombstones remove.
    for (size_t p = 1; p < page_count(); p++) {
        if (page_erased(p)) {
            if (first_free == 0) first_free = p;
            continue;
        }
        const Record& r = record_at(p);
        if (!record_valid(r)) continue;  // torn/aborted append — skip
        if (r.seq > max_seq) max_seq = r.seq;
        if (r.flags & FLAG_DELETED) {
            store.del(key_view(r));
        } else {
            store.put(key_view(r), r.value, r.value_len);
        }
    }
    append_page_ = first_free ? first_free : page_count();  // page_count() == full
    next_seq_ = max_seq + 1;
}

bool Log::put(Store& store, std::string_view key, const uint8_t* value, size_t value_len) {
    if (!store.put(key, value, value_len)) return false;
    Record r;
    fill_record(r, key, value, value_len, 0);
    stamp_and_append(r, store);
    return true;
}

bool Log::del(Store& store, std::string_view key) {
    if (!store.del(key)) return false;  // wasn't present
    Record r;
    fill_record(r, key, nullptr, 0, FLAG_DELETED);
    stamp_and_append(r, store);
    return true;
}

} // namespace kv
