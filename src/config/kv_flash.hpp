#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "config/kv_store.hpp"

// Device flash backend + boot wiring for the k/v config store (E2 S2.3/S2.4).
// The log-structured logic lives in kv_log.{hpp,cpp}; this only supplies the
// on-device flash region and the global store instance.
namespace kv {

// The global configuration store. Valid (loaded from flash) after config_boot().
Store& config();

// Load persisted config from the reserved flash region into config() at boot.
// Called once from main() after display init. Formats the region on first use;
// fails safe (empty store, no flash writes) if the region would overlap the image.
void config_boot();

// Persist a set/delete to config() and flash (thin wrappers over the log). `get`
// is just config().get(...). Return false as kv::Store/Log would (full/too-long,
// or key absent for del).
bool config_put(std::string_view key, const uint8_t* value, size_t value_len);
bool config_del(std::string_view key);

} // namespace kv
