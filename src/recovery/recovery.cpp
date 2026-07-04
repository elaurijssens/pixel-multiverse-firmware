#include "recovery/recovery.hpp"

#if defined(MULTIVERSE_RESET_BTN)

#include "hardware/gpio.h"
#include "pico/time.h"
#include "config/kv_flash.hpp"

namespace recovery {

void check_factory_reset() {
    const uint pin = MULTIVERSE_RESET_BTN;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);            // active-low button to GND
    sleep_ms(5);                  // let the pull settle

    // Require the button held ~300 ms so a brief touch / noise doesn't wipe config.
    for (int i = 0; i < 30; i++) {
        if (gpio_get(pin)) return;   // released (high) → not a reset request
        sleep_ms(10);
    }
    kv::config_factory_reset();      // held → erase config; config_boot() reformats empty
}

}  // namespace recovery

#else  // no reset button configured for this board → no-op

namespace recovery { void check_factory_reset() {} }

#endif
