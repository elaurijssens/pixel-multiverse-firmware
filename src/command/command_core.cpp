#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <string_view>

#include "display.hpp"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/sync.h"
#include "hardware/structs/rosc.h"
#include "hardware/watchdog.h"
#include "zlib.h"

#include "command/command_core.hpp"
#include "command/transport.hpp"

using namespace pimoroni;

namespace {

const size_t COMMAND_LEN = 4;
uint8_t command_buffer[COMMAND_LEN];

uint16_t get_data_uint16(command_core::Transport& transport) {
    uint16_t len;
    transport.poll();
    transport.read((uint8_t *)&len, 2);
    return len;
}

uint8_t get_data_uint8(command_core::Transport& transport) {
    uint8_t len;
    transport.poll();
    transport.read((uint8_t *)&len, 1);
    return len;
}

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

// --- Handlers (one per command; each reads its own payload) -----------------

void handle_data(command_core::Transport& transport) {
    if (transport.read(display::buffer, display::BUFFER_SIZE) == display::BUFFER_SIZE) {
        display::update();
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
    const size_t MAX_COMPRESSED_SIZE = display::BUFFER_SIZE;  // Adjust based on expected maximum
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

    // Decompress the data using zlib
    uLongf decompressed_size = display::BUFFER_SIZE;  // Expected size of decompressed data
    int ret = uncompress(display::buffer, &decompressed_size, compressed_data, compressed_size);

    free(compressed_data);  // Free the compressed data buffer

    if (ret != Z_OK) {
        // Error handling: decompression failed
        // Optionally log the error code ret
        return;
    }

    // Verify that the decompressed size is as expected
    if (decompressed_size != display::BUFFER_SIZE) {
        // Error handling: unexpected decompressed size
        return;
    }

    // Update the display with the decompressed data
    display::update();
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

// The `wave` command (raw PCM streaming via display::play_audio) was dropped in
// S1.4: no host tooling ever sent it, and `note` covers synth audio. play_audio()
// remains in the display API if raw streaming is wanted again later.

void handle_note(command_core::Transport& transport) {
    uint8_t channel = get_data_uint8(transport);
    uint16_t freq = get_data_uint16(transport);

    uint8_t waveform = get_data_uint8(transport);

    uint16_t a = get_data_uint16(transport);
    uint16_t d = get_data_uint16(transport);
    uint16_t s = get_data_uint16(transport);
    uint16_t r = get_data_uint16(transport);

    uint8_t phase = get_data_uint8(transport);

    display::play_note(channel, freq, waveform, a, d, s, r, phase);
    //display::info("note");
}

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

void register_builtins() {
    static bool done = false;
    if (done) return;
    done = true;

    command_core::register_command("data", handle_data);
    command_core::register_command("zdat", handle_zdat);
    command_core::register_command("test", handle_test);
    command_core::register_command("note", handle_note);
    command_core::register_command("_rst", handle_rst);
    command_core::register_command("_usb", handle_usb);
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
