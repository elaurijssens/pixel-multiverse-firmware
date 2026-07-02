#pragma once

#include "config/kv_store.hpp"

// Device flash backend + boot wiring for the k/v config store (E2 S2.3).
// The log-structured logic lives in kv_log.{hpp,cpp}; this only supplies the
// on-device flash region and the global store instance.
namespace kv {

// The global configuration store. Valid (loaded from flash) after config_boot().
Store& config();

// Load persisted config from the reserved flash region into config() at boot.
// Called once from main() after display init. Formats the region on first use;
// fails safe (empty store, no flash writes) if the region would overlap the image.
void config_boot();

} // namespace kv
