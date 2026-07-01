#pragma once

#include <cstddef>
#include <cstdint>

// On-flash layout for the E2 key/value configuration store (S2.1).
//
// The store is a flat array of fixed-size records preceded by a header. Both the
// header and every record are RECORD_SIZE (128) bytes, so record N lives at
// RECORD_SIZE * (N + 1) from the region base — slot 0 is the header. A 4 KB flash
// erase sector therefore holds the header plus 31 records.
//
// Sizing rationale (S2.1 decision): 128 is the smallest power of two that holds
// an 8-byte key + 64-byte value, leaving 56 bytes reserved for future per-record
// metadata (a valid/tombstone flag, CRC, or key length) that can be added later
// without changing the record size or reformatting the store. Power-of-two
// records keep offset math trivial and pack evenly into the 4 KB erase sector.
//
// Values are untyped raw bytes — interpretation is the caller's job (typed values
// are out of scope for E2). Multi-byte header fields are little-endian (device
// native on RP2040/RP2350).
namespace kv {

constexpr size_t KEY_LEN      = 8;    // alphanumeric, space-padded if shorter
constexpr size_t VALUE_LEN    = 64;   // untyped raw bytes
constexpr size_t RESERVED_LEN = 56;   // future per-record metadata; zero today
constexpr size_t RECORD_SIZE  = 128;  // KEY_LEN + VALUE_LEN + RESERVED_LEN

constexpr uint16_t FORMAT_VERSION = 1;
constexpr char     MAGIC[4]       = {'M', 'V', 'K', 'V'};

// key[0] sentinels. Valid keys are alphanumeric, so these never collide with a
// real key. Erased flash reads as 0xFF; deletion writes 0x00 (flash writes can
// only clear bits 1->0, so a tombstone needs no sector erase).
constexpr uint8_t SLOT_ERASED    = 0xFF;  // fresh/empty slot
constexpr uint8_t SLOT_TOMBSTONE = 0x00;  // deleted record

// Region header, occupying slot 0 (the first RECORD_SIZE bytes of the region).
struct Header {
    char     magic[4];       // MAGIC — identifies the store
    uint16_t version;        // FORMAT_VERSION — gates future format changes
    uint16_t record_size;    // RECORD_SIZE — lets a reader sanity-check layout
    uint8_t  reserved[RECORD_SIZE - 8];
};
static_assert(sizeof(Header) == RECORD_SIZE, "kv::Header must be one record slot");

// A single key/value record. A slot is empty when key[0] == SLOT_ERASED and
// deleted when key[0] == SLOT_TOMBSTONE.
struct Record {
    char    key[KEY_LEN];
    uint8_t value[VALUE_LEN];
    uint8_t reserved[RESERVED_LEN];
};
static_assert(sizeof(Record) == RECORD_SIZE, "kv::Record must be RECORD_SIZE bytes");

} // namespace kv
