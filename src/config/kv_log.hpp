#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "config/kv_store.hpp"
#include "config/kv_format.hpp"

// Log-structured persistence logic for the k/v store (E2 S2.3).
//
// This file is SDK-free so it is host-testable: the storage backend is the
// abstract FlashOps, and the CRC-32 function is injected. The device backend
// (real flash + XIP + interrupt-safe erase/program) and boot wiring live in
// kv_flash.{hpp,cpp}.
//
// Layout: the region is an array of 256-byte flash pages, one record per page
// (record in the low RECORD_SIZE bytes, the rest left erased). Page 0 is the
// kv::Header; pages 1.. are the append log. `put`/`del` append a fresh record
// with an increasing `seq` (del appends a FLAG_DELETED tombstone) instead of
// editing in place, so page order == seq order for every valid record. `load`
// scans pages ascending and replays valid records into the Store — later pages
// override earlier, tombstones remove — reconstructing the highest-seq live view.
// When the log is full a `compact()` erases the region and rewrites just the live
// set, so sector erase is rare (wear-levelling) and a torn append simply fails its
// CRC and is skipped (the prior record stays authoritative).
namespace kv {

// Program/erase granularity (mirror of the SDK's FLASH_PAGE_SIZE / _SECTOR_SIZE;
// the device backend static_asserts they match).
constexpr size_t PAGE_SIZE   = 256;
constexpr size_t SECTOR_SIZE = 4096;

// Storage backend. Offsets are relative to the region base.
struct FlashOps {
    virtual ~FlashOps() = default;
    virtual size_t size() const = 0;                          // region size, multiple of SECTOR_SIZE
    virtual const uint8_t* read(size_t offset) const = 0;     // pointer to region+offset
    virtual void erase(size_t offset, size_t len) = 0;        // len multiple of SECTOR_SIZE
    virtual void program(size_t offset, const uint8_t* data, size_t len) = 0;  // len multiple of PAGE_SIZE
};

// CRC-32 (IEEE) over a byte range. Device passes zlib's crc32; the host test
// passes an equivalent implementation.
using Crc32Fn = uint32_t (*)(const uint8_t* data, size_t len);

class Log {
public:
    Log(FlashOps& ops, Crc32Fn crc) : ops_(ops), crc_(crc) {}

    // Rebuild `store` from flash (formatting a blank/foreign region first).
    void load(Store& store);

    // Update RAM + persist. Return false if the RAM store rejects it
    // (value too long / full for a new key) or the key wasn't present (del).
    bool put(Store& store, std::string_view key, const uint8_t* value, size_t value_len);
    bool del(Store& store, std::string_view key);

private:
    size_t page_count() const { return ops_.size() / PAGE_SIZE; }
    const Header& header() const { return *reinterpret_cast<const Header*>(ops_.read(0)); }
    const Record& record_at(size_t page) const {
        return *reinterpret_cast<const Record*>(ops_.read(page * PAGE_SIZE));
    }

    bool header_valid() const;
    bool record_valid(const Record& r) const;
    bool page_erased(size_t page) const;

    void write_slot(size_t page, const void* rec128);  // 128-byte record/header -> a page
    void format(Store& store);                         // erase, write header, empty store
    void compact(Store& store);                        // erase, header + live records
    void stamp_and_append(Record& r, Store& store);    // seq+crc, append (compacting if full)
    void fill_record(Record& r, std::string_view key, const uint8_t* value,
                     size_t value_len, uint8_t flags) const;

    FlashOps& ops_;
    Crc32Fn   crc_;
    size_t    append_page_ = 1;  // next free page (== page_count() when full)
    uint32_t  next_seq_    = 1;
};

} // namespace kv
