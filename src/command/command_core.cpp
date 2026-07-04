#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <string_view>

#include "display.hpp"
#include "version.hpp"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/sync.h"
#include "hardware/structs/rosc.h"
#include "hardware/watchdog.h"
#include "zlib.h"

#include "command/command_core.hpp"
#include "command/transport.hpp"
#include "config/kv_flash.hpp"
#include "net/wifi.hpp"

using namespace pimoroni;

namespace {

const size_t COMMAND_LEN = 4;
uint8_t command_buffer[COMMAND_LEN];

// --- Command registry: 4-byte id -> handler ---------------------------------

struct Entry {
    char id[COMMAND_LEN];
    command_core::Handler fn;
};

const size_t MAX_COMMANDS = 16;  // headroom for E2 k/v commands
Entry entries[MAX_COMMANDS];
size_t entry_count = 0;

command_core::Handler lookup(const uint8_t id[COMMAND_LEN]) {
    for (size_t i = 0; i < entry_count; i++) {
        if (memcmp(entries[i].id, id, COMMAND_LEN) == 0) {
            return entries[i].fn;
        }
    }
    return nullptr;
}

// E6 double buffering: `data`/`zdat` always load the back (hidden) buffer. In
// immediate mode (default, "live") they present it right away; in deferred mode
// ("hold") they don't — a `flip` command presents it, so many boards can load
// then flip together (E7).
bool deferred = false;

// --- Handlers (one per command; each reads its own payload) -----------------

void handle_data(command_core::Transport& transport) {
    if (transport.read(display::back(), display::buffer_size()) == display::buffer_size()) {
        if (!deferred) display::update();
    }
}

void handle_zdat(command_core::Transport& transport) {
    // Read the size of the compressed data (4 bytes)
    uint32_t compressed_size;
    if (transport.read((uint8_t*)&compressed_size, sizeof(compressed_size)) != sizeof(compressed_size)) {
        // Error handling: failed to read compressed size
        display::info("nosize");
        return;
    }

    // Ensure compressed_size is within reasonable limits
    const size_t MAX_COMPRESSED_SIZE = display::buffer_size();  // Adjust based on expected maximum
    if (compressed_size > MAX_COMPRESSED_SIZE) {
        // Error handling: compressed data too large
        return;
    }

    // Allocate buffer for compressed data
    uint8_t* compressed_data = (uint8_t*)malloc(compressed_size);
    if (!compressed_data) {
        // Error handling: insufficient memory
        return;
    }

    // Read the compressed data
    if (transport.read(compressed_data, compressed_size) != compressed_size) {
        // Error handling: failed to read compressed data
        free(compressed_data);
        return;
    }

    // Decompress the data using zlib into the back buffer
    uLongf decompressed_size = display::buffer_size();  // Expected size of decompressed data
    int ret = uncompress(display::back(), &decompressed_size, compressed_data, compressed_size);

    free(compressed_data);  // Free the compressed data buffer

    if (ret != Z_OK) {
        // Error handling: decompression failed
        // Optionally log the error code ret
        return;
    }

    // Verify that the decompressed size is as expected
    if (decompressed_size != display::buffer_size()) {
        // Error handling: unexpected decompressed size
        return;
    }

    // Present the decompressed frame (unless holding for a flip)
    if (!deferred) display::update();
}

void handle_test(command_core::Transport& transport) {
    // Two ASCII digits select the self-test pattern (00-99), so many
    // tests share one 4-byte command id. See display/selftest.hpp for
    // the pattern catalogue (channel/row/column/geometry checks).
    uint8_t digits[2];
    transport.poll();
    if (transport.read(digits, 2) != 2) {
        return;
    }
    if (digits[0] < '0' || digits[0] > '9' ||
        digits[1] < '0' || digits[1] > '9') {
        display::info("test?");
        return;
    }
    uint8_t test_id = (digits[0] - '0') * 10 + (digits[1] - '0');
    display::selftest(test_id);
}

// Audio (the `note`/`wave` commands + display::play_note/play_audio) was removed in
// S9.2: only the Unicorn boards had synth/sample support, and they were dropped.

void handle_rst(command_core::Transport& transport) {
    display::info("RST");
    sleep_ms(500);
    save_and_disable_interrupts();
    rosc_hw->ctrl = ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB;
    watchdog_reboot(0, 0, 0);
}

void handle_usb(command_core::Transport& transport) {
    display::info("USB");
    sleep_ms(500);
    save_and_disable_interrupts();
    rosc_hw->ctrl = ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB;
    reset_usb_boot(0, 0);
}

// E12 S12.1: erase the config store (factory reset), then reboot onto defaults.
void handle_fac(command_core::Transport& transport) {
    display::info("factory reset");
    sleep_ms(600);
    kv::config_factory_reset();
    sleep_ms(50);
    save_and_disable_interrupts();
    rosc_hw->ctrl = ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB;
    watchdog_reboot(0, 0, 0);
}

// E6: present the loaded back buffer (used with `hold` for load-then-flip sync).
void handle_flip(command_core::Transport& transport) {
    display::update();
}

// E6: enter deferred mode — `data`/`zdat` load without presenting until `flip`.
void handle_hold(command_core::Transport& transport) {
    deferred = true;
}

// E6: return to immediate mode — `data`/`zdat` present each frame (the default).
void handle_live(command_core::Transport& transport) {
    deferred = false;
}

// Diagnostics / E8 S8.2: report firmware identity + geometry as a u16
// length-prefixed ASCII blob ("key=value\n" lines). Read by `multiverse-ctl diag`.
void handle_vers(command_core::Transport& transport) {
    static char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "board=%s\nversion=%s\ndisplay=%dx%d\nbufsize=%u\n",
                     MULTIVERSE_BOARD_ID, MULTIVERSE_VERSION,
                     display::width(), display::height(),
                     static_cast<unsigned>(display::buffer_size()));
    if (n < 0) n = 0;
    if (net::wifi_enabled() && n < static_cast<int>(sizeof(buf))) {
        n += snprintf(buf + n, sizeof(buf) - n, "wifi=%s\nip=%s\n",
                      net::wifi_connected() ? "connected" : "connecting",
                      net::wifi_ip());
    }
    if (n > static_cast<int>(sizeof(buf))) n = static_cast<int>(sizeof(buf));
    uint8_t hdr[2] = { static_cast<uint8_t>(n & 0xff), static_cast<uint8_t>((n >> 8) & 0xff) };
    transport.write(hdr, 2);
    transport.write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n));
}

void register_builtins() {
    static bool done = false;
    if (done) return;
    done = true;

    command_core::register_command("data", handle_data);
    command_core::register_command("zdat", handle_zdat);
    command_core::register_command("test", handle_test);
    command_core::register_command("flip", handle_flip);
    command_core::register_command("hold", handle_hold);
    command_core::register_command("live", handle_live);
    command_core::register_command("vers", handle_vers);
    command_core::register_command("_rst", handle_rst);
    command_core::register_command("_usb", handle_usb);
    command_core::register_command("_fac", handle_fac);
}

} // namespace

namespace command_core {

bool register_command(const char id[COMMAND_LEN], Handler handler) {
    if (entry_count >= MAX_COMMANDS) return false;
    if (lookup((const uint8_t *)id) != nullptr) return false;  // already registered
    memcpy(entries[entry_count].id, id, COMMAND_LEN);
    entries[entry_count].fn = handler;
    entry_count++;
    return true;
}

void run(Transport& transport) {
    register_builtins();

    while (1) {
        transport.poll();
        net::wifi_poll();  // service CYW43 + lwIP (poll mode); no-op on non-W builds

        if(!transport.wait_for("multiverse:")) {
            //display::info("mto");
            continue; // Couldn't get the framing prefix
        }

        if(transport.read(command_buffer, COMMAND_LEN) != COMMAND_LEN) {
            //display::info("cto");
            continue;
        }

        Handler handler = lookup(command_buffer);
        if (handler != nullptr) {
            handler(transport);
        }
        // Unknown/garbage id: no handler — fall through and re-sync on the
        // next `multiverse:` prefix.
    }
}

} // namespace command_core
