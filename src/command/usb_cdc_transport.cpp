#include <string.h>
#include <algorithm>

#include "pico/timeout_helper.h"

#include "bsp/board.h"
#include "tusb.h"

#include "cdc_uart.h"

#include "command/usb_cdc_transport.hpp"

namespace command_core {

void UsbCdcTransport::poll() {
    tud_task();
}

bool UsbCdcTransport::wait_for(std::string_view data, uint32_t timeout_ms) {
    timeout_state ts;
    absolute_time_t until = delayed_by_ms(get_absolute_time(), timeout_ms);
    check_timeout_fn check_timeout = init_single_timeout_until(&ts, until);

    for(auto expected_char : data) {
        char got_char;
        while(1){
            tud_task();
            if (cdc_task((uint8_t *)&got_char, 1) == 1) break;
            if(check_timeout(&ts, until)) return false;
        }
        if (got_char != expected_char) return false;
    }
    return true;
}

size_t UsbCdcTransport::read(uint8_t *buffer, size_t len, uint32_t timeout_ms) {
    memset((void *)buffer, 0, len);

    uint8_t *p = buffer;

    timeout_state ts;
    absolute_time_t until = delayed_by_ms(get_absolute_time(), timeout_ms);
    check_timeout_fn check_timeout = init_single_timeout_until(&ts, until);

    size_t bytes_remaining = len;
    while (bytes_remaining && !check_timeout(&ts, until)) {
        tud_task(); // tinyusb device task
        size_t bytes_read = cdc_task(p, std::min(bytes_remaining, MAX_UART_PACKET));
        bytes_remaining -= bytes_read;
        p += bytes_read;
    }
    return len - bytes_remaining;
}

size_t UsbCdcTransport::write(const uint8_t *buffer, size_t len) {
    size_t written = tud_cdc_write(buffer, len);
    tud_cdc_write_flush();
    return written;
}

} // namespace command_core
