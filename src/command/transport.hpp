#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace command_core {

// Transport-agnostic byte I/O for the command core. A transport supplies and
// accepts bytes; the core frames and dispatches commands without knowing how
// they arrive. USB CDC is the first implementation (S1.2); WiFi is a later one
// (E7). The core depends only on this interface, never on TinyUSB directly.
class Transport {
public:
    virtual ~Transport() = default;

    // Pump the transport's housekeeping (USB: tud_task()). Called once per
    // command-loop iteration.
    virtual void poll() = 0;

    // Block until the exact byte sequence `data` has been received, or the
    // timeout elapses. Returns true on a full match, false on timeout/mismatch.
    virtual bool wait_for(std::string_view data, uint32_t timeout_ms = 1000) = 0;

    // Read up to `len` bytes into `buffer`, returning the number actually read
    // before the timeout. `buffer` is zeroed first.
    virtual size_t read(uint8_t *buffer, size_t len, uint32_t timeout_ms = 1000) = 0;

    // Write `len` bytes back to the host (for get/diagnostics responses).
    // Returns the number of bytes written.
    virtual size_t write(const uint8_t *buffer, size_t len) = 0;
};

} // namespace command_core
