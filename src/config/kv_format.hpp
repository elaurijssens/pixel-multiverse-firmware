#pragma once

#include <cstddef>  // offsetof
#include <cstdint>

// On-flash layout for the E2 key/value configuration store (S2.1).
//
// The store is a flat array of fixed-size records preceded by a header. Both the
// header and every record are RECORD_SIZE (128) bytes, so record N lives at
// RECORD_SIZE * (N + 1) from the region base — slot 0 is the header. A 4 KB flash
// erase sector therefore holds the header plus 31 records.
//
// Sizing rationale (S2.1 decision): 128 is the smallest power of two that holds
// an 8-byte key + 64-byte value with room for per-record metadata. Power-of-two
// records keep offset math trivial and pack evenly into the 4 KB erase sector.
//
// Integrity: the last 4 bytes of every slot are a CRC-32 (IEEE 802.3, zlib-
// compatible — the firmware already links zlib's crc32()) over the preceding
// CRC_COVERAGE (124) bytes, i.e. the whole slot except the CRC word itself. So
// the key, value, the length fields, and any future metadata placed in `reserved`
// are all covered automatically. This detects bit-rot and gives power-loss atomicity: a
// record whose write was interrupted fails its CRC and is treated as invalid
// rather than silently returning garbage.
//
// Values are untyped raw bytes — interpretation is the caller's job (typed values
// are out of scope for E2). Multi-byte fields are little-endian (device native on
// RP2040/RP2350).
namespace kv {

constexpr size_t KEY_LEN     = 8;    // key field width; content is alphanumeric, space-padded
constexpr size_t VALUE_LEN   = 64;   // untyped raw bytes
constexpr size_t CRC_LEN     = 4;    // trailing CRC-32
constexpr size_t RECORD_SIZE = 128;

// The CRC covers everything in the slot except its own trailing word.
constexpr size_t CRC_COVERAGE = RECORD_SIZE - CRC_LEN;  // 124

// Per-record reserved metadata (after the length fields, before the CRC);
// CRC-covered. New fields can be carved from here later without moving key,
// value, seq, the length fields, or the trailing CRC.
constexpr size_t RESERVED_LEN =
    RECORD_SIZE - KEY_LEN - VALUE_LEN - 4 /*seq*/ - 1 /*key_len*/ - 1 /*value_len*/ - CRC_LEN;  // 46

constexpr uint16_t FORMAT_VERSION = 1;
constexpr char     MAGIC[4]       = {'M', 'V', 'K', 'V'};

// key[0] sentinels. Valid keys are alphanumeric, so these never collide with a
// real key. Erased flash reads as 0xFF, marking a free slot to append into. In
// the log-structured store, deletion appends a higher-seq tombstone record for
// the key rather than editing in place; SLOT_TOMBSTONE is the reserved delete
// marker whose exact use is finalised in S2.3.
constexpr uint8_t SLOT_ERASED    = 0xFF;  // fresh/empty slot (append target)
constexpr uint8_t SLOT_TOMBSTONE = 0x00;  // reserved delete marker (S2.3)

// Region header, occupying slot 0 (the first RECORD_SIZE bytes of the region).
struct Header {
    char     magic[4];       // MAGIC — identifies the store
    uint16_t version;        // FORMAT_VERSION — gates future format changes
    uint16_t record_size;    // RECORD_SIZE — lets a reader sanity-check layout
    uint8_t  reserved[RECORD_SIZE - 8 - CRC_LEN];
    uint32_t crc32;          // CRC-32 over the preceding CRC_COVERAGE bytes
};
static_assert(offsetof(Header, crc32) == CRC_COVERAGE, "kv::Header.crc32 must be the trailing word");
static_assert(sizeof(Header) == RECORD_SIZE, "kv::Header must be one record slot");

// A single key/value record. A slot is empty when key[0] == SLOT_ERASED and
// deleted when key[0] == SLOT_TOMBSTONE; otherwise it is valid only if crc32
// matches CRC-32 over the leading CRC_COVERAGE bytes. `key_len`/`value_len` give
// the number of significant key/value bytes, disambiguating real content from
// padding (keys) or unused tail (values).
//
// `seq` is the write sequence for the log-structured / wear-levelled store: a
// `put` appends a new record with a higher `seq` rather than erasing in place, so
// on load the highest-`seq` valid record for a key wins (finalised in S2.3).
struct Record {
    char     key[KEY_LEN];
    uint8_t  value[VALUE_LEN];
    uint32_t seq;                     // write sequence — highest valid seq per key wins
    uint8_t  key_len;                 // significant key bytes (1..KEY_LEN)
    uint8_t  value_len;               // significant value bytes (0..VALUE_LEN)
    uint8_t  reserved[RESERVED_LEN];  // future metadata; zero today (CRC-covered)
    uint32_t crc32;                   // CRC-32 over the preceding CRC_COVERAGE bytes
};
static_assert(offsetof(Record, value) == KEY_LEN, "kv::Record.value must follow the key");
static_assert(offsetof(Record, seq) == KEY_LEN + VALUE_LEN, "kv::Record.seq must follow key+value (4-aligned)");
static_assert(offsetof(Record, key_len) == KEY_LEN + VALUE_LEN + 4, "kv::Record.key_len must follow seq");
static_assert(offsetof(Record, value_len) == KEY_LEN + VALUE_LEN + 5, "kv::Record.value_len must follow key_len");
static_assert(offsetof(Record, crc32) == CRC_COVERAGE, "kv::Record.crc32 must be the trailing word");
static_assert(sizeof(Record) == RECORD_SIZE, "kv::Record must be RECORD_SIZE bytes");

} // namespace kv
