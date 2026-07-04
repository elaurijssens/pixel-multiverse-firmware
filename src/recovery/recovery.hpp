#pragma once

// Early-boot factory-reset escape hatch (E12 S12.2). If the board's reset button
// is held at power-on, erase the config store *before it is read*, so a config
// that wedges init() can't re-brick the board (a USB command can't help there).
// Board-specific pin via MULTIVERSE_RESET_BTN; a no-op when undefined. Must be
// called before kv::config_boot().

namespace recovery {
    void check_factory_reset();
}
