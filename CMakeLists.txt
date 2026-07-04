cmake_minimum_required(VERSION 3.12)

if(DEFINED ENV{MULTIVERSE_BOARD})
    set(MULTIVERSE_BOARD $ENV{MULTIVERSE_BOARD})
endif()

if(NOT DEFINED MULTIVERSE_BOARD)
set(MULTIVERSE_BOARD i75)
message(WARNING "No \"MULTIVERSE_BOARD\" specified. Defaulting to \"i75\" (supported: i75, plasma)")
else()
message("\"MULTIVERSE_BOARD\" is set to \"${MULTIVERSE_BOARD}\".")
endif()

set(NAME ${MULTIVERSE_BOARD}-multiverse)
set(DISPLAY_PATH ${CMAKE_CURRENT_LIST_DIR}/src/display/${MULTIVERSE_BOARD}/${MULTIVERSE_BOARD}.cmake)

# Firmware version string from git (tag-relative, short hash, +dirty marker).
execute_process(
    COMMAND git -C ${CMAKE_CURRENT_LIST_DIR} describe --tags --always --dirty
    OUTPUT_VARIABLE MULTIVERSE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT MULTIVERSE_VERSION)
    set(MULTIVERSE_VERSION "unknown")
endif()
message("Firmware version: ${MULTIVERSE_VERSION}")

include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)
include(zlib_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Tiny USB
set(FAMILY rp2040)
set(BOARD pico_sdk)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

# Add your source files
add_executable(${NAME}
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/command/command_core.cpp
    ${SRC_DIR}/command/usb_cdc_transport.cpp
    ${SRC_DIR}/config/kv_store.cpp
    ${SRC_DIR}/config/kv_log.cpp
    ${SRC_DIR}/config/kv_flash.cpp
    ${SRC_DIR}/config/kv_commands.cpp
    ${SRC_DIR}/cdc_uart.cpp
    ${SRC_DIR}/get_serial.cpp
    ${SRC_DIR}/usb_descriptors.cpp
)

target_include_directories(
    ${NAME} PRIVATE
    ${SRC_DIR}
)

# Board identity: board-chip (e.g. i75-rp2350). Chip family from the SDK platform,
# which is known now (after pico_sdk_init).
if(PICO_PLATFORM MATCHES "rp2350")
    set(MULTIVERSE_CHIP "rp2350")
else()
    set(MULTIVERSE_CHIP "rp2040")
endif()
set(MULTIVERSE_BOARD_ID "${MULTIVERSE_BOARD}-${MULTIVERSE_CHIP}")
message("Board id: ${MULTIVERSE_BOARD_ID}")

target_compile_definitions(
    ${NAME} PRIVATE
    MULTIVERSE_BOARD="${MULTIVERSE_BOARD}"
    MULTIVERSE_BOARD_ID="${MULTIVERSE_BOARD_ID}"
    MULTIVERSE_VERSION="${MULTIVERSE_VERSION}"
)

include(${DISPLAY_PATH})

# Don't forget to link the libraries you need!
target_link_libraries(${NAME}
    display
    zlib

    pico_stdlib
    pico_unique_id
    hardware_pio
    hardware_flash
    hardware_watchdog

    tinyusb_device
    tinyusb_board
)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

# Release bundle: just the firmware image + docs. CI (.github/workflows/cmake.yml)
# attaches the version-stamped .uf2 to releases directly; CPack is kept so a
# `--target package` still yields a self-contained zip/tgz. The legacy upstream
# Python examples are intentionally not packaged (superseded by the separate
# pixel-multiverse host driver, and slated for removal).
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    ${CMAKE_CURRENT_LIST_DIR}/LICENSE
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
