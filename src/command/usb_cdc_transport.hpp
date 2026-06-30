#pragma once

#include "command/transport.hpp"

namespace command_core {

// Transport over USB CDC. Wraps the existing `cdc_*` helpers and TinyUSB so the
// command core stays free of any direct USB dependency. Behaviour matches the
// original inline `cdc_wait_for` / `cdc_get_bytes` helpers exactly.
class UsbCdcTransport : public Transport {
public:
    void poll() override;
    bool wait_for(std::string_view data, uint32_t timeout_ms = 1000) override;
    size_t read(uint8_t *buffer, size_t len, uint32_t timeout_ms = 1000) override;
    size_t write(const uint8_t *buffer, size_t len) override;
};

} // namespace command_core
