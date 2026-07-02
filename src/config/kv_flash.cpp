#include "config/kv_flash.hpp"
#include "config/kv_log.hpp"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"  // XIP_BASE
#include "pico/config.h"               // PICO_FLASH_SIZE_BYTES (from the board header)
#include "zlib.h"

// Linker symbol: first byte past the program image in flash. Declared at global
// scope with C linkage so it resolves to the unmangled linker-defined symbol.
extern "C" char __flash_binary_end;

namespace kv {
namespace {

// Reserve the top of flash for the config store, clear of the program image
// (which the SDK links from the bottom). Board-agnostic via the SDK size macro
// (2 MB RP2040 / 4 MB RP2350).
constexpr size_t   REGION_SIZE   = 16 * 1024;  // 4 sectors
constexpr uint32_t REGION_OFFSET = PICO_FLASH_SIZE_BYTES - REGION_SIZE;

static_assert(PAGE_SIZE == FLASH_PAGE_SIZE, "kv::PAGE_SIZE must match the SDK");
static_assert(SECTOR_SIZE == FLASH_SECTOR_SIZE, "kv::SECTOR_SIZE must match the SDK");
static_assert(REGION_SIZE % SECTOR_SIZE == 0, "region must be sector-aligned");

uint32_t crc32_ieee(const uint8_t* data, size_t len) {
    return static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(data),
                                       static_cast<uInt>(len)));
}

// Flash-backed FlashOps. Reads are memory-mapped (XIP); erase/program run with
// interrupts disabled (single-core firmware — no other core to lock out).
class DeviceFlashOps : public FlashOps {
public:
    size_t size() const override { return REGION_SIZE; }

    const uint8_t* read(size_t offset) const override {
        return reinterpret_cast<const uint8_t*>(XIP_BASE + REGION_OFFSET + offset);
    }

    void erase(size_t offset, size_t len) override {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(REGION_OFFSET + offset, len);
        restore_interrupts(ints);
    }

    void program(size_t offset, const uint8_t* data, size_t len) override {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(REGION_OFFSET + offset, data, len);
        restore_interrupts(ints);
    }
};

DeviceFlashOps g_ops;
Store          g_store;
Log            g_log(g_ops, crc32_ieee);

bool region_overlaps_image() {
    uintptr_t image_end_off = reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
    return image_end_off > REGION_OFFSET;
}

} // namespace

Store& config() { return g_store; }

void config_boot() {
    if (region_overlaps_image()) {
        g_store.clear();  // fail safe: never write flash that holds the program
        return;
    }
    g_log.load(g_store);
}

} // namespace kv
