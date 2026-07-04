/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "display.hpp"

#include "bsp/board.h"
#include "tusb.h"
#include "hardware/watchdog.h"

#include "get_serial.h"

#include "command/command_core.hpp"
#include "command/usb_cdc_transport.hpp"
#include "config/kv_flash.hpp"
#include "config/kv_commands.hpp"
#include "net/wifi.hpp"
#include "net/udp_transport.hpp"
#include "net/multicast.hpp"
#include "recovery/recovery.hpp"

// UART0 for Picoprobe debug
// UART1 for picoprobe to target device

using namespace pimoroni;

int main(void) {
    board_init(); // Wtf?
    usb_serial_init(); // ??
    //cdc_uart_init(); // From cdc_uart.c
    tusb_init(); // Tiny USB?

    recovery::check_factory_reset();  // hold the reset button at boot → wipe config first
    kv::config_boot();       // load persisted config from flash first (formats on first use)
    display::init();         // reads panel dimensions from the config store
    kv::register_commands(); // put/get/del, before run() registers the built-ins
    net::wifi_init();        // W images only: connect if `wifi` enabled (no-op otherwise)
    net::udp_transport_init(); // bind the UDP command socket (W builds, if wifi up)
    net::multicast_init();     // join the multicast frame group (E7 S7.3)

    // Safety net: reboot if the command loop wedges for >4s (e.g. a CYW43 stall).
    // Every handler + read completes well inside this; run() pets it each iteration.
    watchdog_enable(4000, true);

    command_core::UsbCdcTransport transport;
    command_core::run(transport, net::udp_transport());  // USB + (on W) UDP

    return 0;
}