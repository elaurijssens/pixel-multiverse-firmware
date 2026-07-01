#include <stdlib.h>
#include <stdio.h>
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
std::string_view command((const char *)command_buffer, COMMAND_LEN);

//uint16_t audio_buffer[22050] = {0};

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

} // namespace

namespace command_core {

void run(Transport& transport) {
    while (1) {
        transport.poll();

        if(!transport.wait_for("multiverse:")) {
            //display::info("mto");
            continue; // Couldn't get 16 bytes of command
        }

        if(transport.read(command_buffer, COMMAND_LEN) != COMMAND_LEN) {
            //display::info("cto");
            continue;
        }

        if(command == "data") {
            if (transport.read(display::buffer, display::BUFFER_SIZE) == display::BUFFER_SIZE) {
                display::update();
            }
            continue;
        }

        if(command == "zdat") {
            // Read the size of the compressed data (4 bytes)
            uint32_t compressed_size;
            if (transport.read((uint8_t*)&compressed_size, sizeof(compressed_size)) != sizeof(compressed_size)) {
                // Error handling: failed to read compressed size
                display::info("nosize");
                continue;
            }

            // Ensure compressed_size is within reasonable limits
            const size_t MAX_COMPRESSED_SIZE = display::BUFFER_SIZE;  // Adjust based on expected maximum
            if (compressed_size > MAX_COMPRESSED_SIZE) {
                // Error handling: compressed data too large
                continue;
            }

            // Allocate buffer for compressed data
            uint8_t* compressed_data = (uint8_t*)malloc(compressed_size);
            if (!compressed_data) {
                // Error handling: insufficient memory
                continue;
            }

            // Read the compressed data
            if (transport.read(compressed_data, compressed_size) != compressed_size) {
                // Error handling: failed to read compressed data
                free(compressed_data);
                continue;
            }

            // Decompress the data using zlib
            uLongf decompressed_size = display::BUFFER_SIZE;  // Expected size of decompressed data
            int ret = uncompress(display::buffer, &decompressed_size, compressed_data, compressed_size);

            free(compressed_data);  // Free the compressed data buffer

            if (ret != Z_OK) {
                // Error handling: decompression failed
                // Optionally log the error code ret
                continue;
            }

            // Verify that the decompressed size is as expected
            if (decompressed_size != display::BUFFER_SIZE) {
                // Error handling: unexpected decompressed size
                continue;
            }

            // Update the display with the decompressed data
            display::update();

            continue;
        }

        if(command == "test") {
            // Two ASCII digits select the self-test pattern (00-99), so many
            // tests share one 4-byte command id. See display/selftest.hpp for
            // the pattern catalogue (channel/row/column/geometry checks).
            uint8_t digits[2];
            transport.poll();
            if (transport.read(digits, 2) != 2) {
                continue;
            }
            if (digits[0] < '0' || digits[0] > '9' ||
                digits[1] < '0' || digits[1] > '9') {
                display::info("test?");
                continue;
            }
            uint8_t test_id = (digits[0] - '0') * 10 + (digits[1] - '0');
            display::selftest(test_id);
            continue;
        }

        /*if(command == "wave") {
            uint16_t audio_len = get_data_uint16(transport);
            if (transport.read((uint8_t *)audio_buffer, audio_len) == audio_len) {
                display::play_audio((uint8_t *)audio_buffer, audio_len / 2);
            }
            continue;
        }*/

        if(command == "note") {
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

        if(command == "_rst") {
            display::info("RST");
            sleep_ms(500);
            save_and_disable_interrupts();
            rosc_hw->ctrl = ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB;
            watchdog_reboot(0, 0, 0);
            continue;
        }

        if(command == "_usb") {
            display::info("USB");
            sleep_ms(500);
            save_and_disable_interrupts();
            rosc_hw->ctrl = ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB;
            reset_usb_boot(0, 0);
            continue;
        }
    }
}

} // namespace command_core
