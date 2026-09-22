cmake_minimum_required(VERSION 3.13)

add_subdirectory(usb_midi_host)

add_compile_definitions(PICO_DEFAULT_UART_BAUD_RATE=921600)
add_compile_definitions(PERSISTED_CONFIG_SIZE=4096)
add_compile_definitions(PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/tinyusb")
set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/Pico-PIO-USB")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/src/boards")

include(pico_sdk_import.cmake)

project(remapper)

pico_sdk_init()

check_and_add_pico_pio_usb_support()

add_compile_options(-Wall)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++17>)

# Emit B-side USB host enumeration diagnostics over the A-side vendor HID
# configuration interface.  Disabled by default so release firmware and its
# descriptor remain unchanged.
option(HID_HOST_DIAGNOSTICS "Enable dual A/B USB host diagnostic reports" OFF)
option(MOUSE_PIPELINE_TRACE "Enable frozen dual A/B mouse pipeline trace rings" OFF)
option(ALT_NUMPAD_EQUALS "Translate RealForce Alt+Numpad6+Numpad1 to '='" OFF)
option(STABLE_CONSUMER_DESCRIPTOR "Use the pre-media-expansion Consumer Control descriptor" OFF)
option(STABLE_MOUSE_BUTTON_DESCRIPTOR "Use the three-button mouse descriptor" OFF)
option(STABLE_A_C436_BASELINE "Build dual A with the pre-c436 descriptor and NumLock baseline" OFF)
option(LEGACY_RELATIVE_AGGREGATION "Use the pre-c436 relative-report aggregation on dual A" OFF)
set(DUAL_A_SERIAL_DRAIN_LIMIT 1 CACHE STRING
    "Maximum consecutive input packets drained by dual A per main-loop pass")

if((PICO_BOARD STREQUAL "pico") OR (PICO_BOARD STREQUAL "pico2"))
add_compile_definitions(GPIO_VALID_PINS_BASE=0b00011100011111111111111111111111)
endif()


add_executable(remapper
    src/main.cc
    src/remapper.cc
    src/remapper_single.cc
    src/crc.cc
    src/descriptor_parser.cc
    src/tinyusb_stuff.cc
    src/our_descriptor.cc
    src/globals.cc
    src/config.cc
    src/quirks.cc
    src/interval_override.cc
    src/out_report.cc
    src/tick.cc
    src/activity_led.cc
    src/ps_auth.cc
    src/app_driver.cc
    src/xbox.cc
)

if((PICO_BOARD STREQUAL "pico") OR (PICO_BOARD STREQUAL "pico2"))
target_compile_definitions(remapper PUBLIC PICO_DEFAULT_UART_TX_PIN=16)
target_compile_definitions(remapper PUBLIC PICO_DEFAULT_UART_RX_PIN=17)
target_compile_definitions(remapper PUBLIC PICO_DEFAULT_PIO_USB_DP_PIN=0)
endif()

target_include_directories(remapper PRIVATE
    src
    src/tusb_config_both
    ${PICO_PIO_USB_PATH}
)

target_link_libraries(remapper
    pico_stdlib
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_flash
    $<$<STREQUAL:${PICO_BOARD},remapper_v8>:hardware_adc>
    tinyusb_device
    tinyusb_host
    tinyusb_board
    tinyusb_pico_pio_usb
    usb_midi_host
)

pico_set_binary_type(remapper copy_to_ram)
if(PICO_PLATFORM STREQUAL "rp2040")
    pico_set_linker_script(remapper ${CMAKE_CURRENT_LIST_DIR}/remapper_single.ld)
elseif(PICO_PLATFORM STREQUAL "rp2350-arm-s")
    pico_set_linker_script(remapper ${CMAKE_CURRENT_LIST_DIR}/remapper_single_rp2350.ld)
else()
    message(FATAL_ERROR "Unknown PICO_PLATFORM.")
endif()
pico_add_extra_outputs(remapper)


add_executable(remapper_dual_a
    src/main.cc
    src/remapper.cc
    src/remapper_dual_a.cc
    src/crc.cc
    src/descriptor_parser.cc
    src/tinyusb_stuff.cc
    src/our_descriptor.cc
    src/globals.cc
    src/config.cc
    src/quirks.cc
    src/interval_override.cc
    src/serial.cc
    src/tick.cc
    src/activity_led.cc
    src/pico_debug/swd.c
    src/pico_debug/flash.c
    src/pico_debug/adi.c
    dual_b_binary.c
    src/ps_auth.cc
    $<$<STREQUAL:${PICO_BOARD},remapper_v7>:src/i2c.cc>
    $<$<STREQUAL:${PICO_BOARD},remapper_v7>:src/mcp4651.cc>
)
target_compile_definitions(remapper_dual_a PRIVATE WBT2_BOOT_INTERFACES=1)
if(STABLE_A_C436_BASELINE)
    # This comparison switch intentionally affects A only. B remains the
    # current trace host binary embedded through dual_b_binary.c.
    target_compile_definitions(remapper_dual_a PRIVATE
        DUAL_A_SERIAL_DRAIN_LIMIT=1
        STABLE_MOUSE_BUTTON_DESCRIPTOR=1
        STABLE_CONSUMER_DESCRIPTOR=1
        STABLE_BOOT_KEYBOARD_DESCRIPTOR=1
        STABLE_NUM_LOCK_INFERENCE=1)
else()
    target_compile_definitions(remapper_dual_a PRIVATE
        DUAL_A_SERIAL_DRAIN_LIMIT=${DUAL_A_SERIAL_DRAIN_LIMIT})
endif()
if(LEGACY_RELATIVE_AGGREGATION)
    target_compile_definitions(remapper_dual_a PRIVATE LEGACY_RELATIVE_AGGREGATION=1)
endif()
if(HID_HOST_DIAGNOSTICS)
    target_compile_definitions(remapper_dual_a PRIVATE HID_HOST_DIAGNOSTICS=1)
endif()
if(MOUSE_PIPELINE_TRACE)
    target_sources(remapper_dual_a PRIVATE src/mouse_pipeline_trace.cc)
    target_compile_definitions(remapper_dual_a PRIVATE
        MOUSE_PIPELINE_TRACE=1 MOUSE_PIPELINE_TRACE_SOURCE=0 MOUSE_PIPELINE_TRACE_CAPACITY=512)
endif()
if(ALT_NUMPAD_EQUALS AND NOT STABLE_A_C436_BASELINE)
    target_compile_definitions(remapper_dual_a PRIVATE ALT_NUMPAD_EQUALS=1)
endif()
if(STABLE_CONSUMER_DESCRIPTOR AND NOT STABLE_A_C436_BASELINE)
    target_compile_definitions(remapper_dual_a PRIVATE STABLE_CONSUMER_DESCRIPTOR=1)
endif()
if(STABLE_MOUSE_BUTTON_DESCRIPTOR AND NOT STABLE_A_C436_BASELINE)
    target_compile_definitions(remapper_dual_a PRIVATE STABLE_MOUSE_BUTTON_DESCRIPTOR=1)
endif()
target_include_directories(remapper_dual_a PRIVATE
    src
    src/tusb_config_device
    src/pico_debug
)
target_link_libraries(remapper_dual_a
    pico_stdlib
    hardware_flash
    $<$<STREQUAL:${PICO_BOARD},remapper_v7>:hardware_i2c>
    hardware_pio
    tinyusb_device
    tinyusb_board
    pico_unique_id
)
pico_generate_pio_header(remapper_dual_a ${CMAKE_CURRENT_LIST_DIR}/src/pico_debug/swd.pio)
pico_add_extra_outputs(remapper_dual_a)

add_executable(remapper_dual_b
    src/remapper_dual_b.cc
    src/crc.cc
    src/interval_override.cc
    src/serial.cc
    src/out_report.cc
    src/activity_led.cc
    src/app_driver.cc
    src/xbox.cc
)
target_include_directories(remapper_dual_b PRIVATE
    src
    src/tusb_config_host
)
if(HID_HOST_DIAGNOSTICS)
    target_compile_definitions(remapper_dual_b PRIVATE HID_HOST_DIAGNOSTICS=1)
    # The pre-mount evidence is emitted from TinyUSB host internals, so the
    # host library must receive the diagnostic-only definition as well.
    target_compile_definitions(tinyusb_host INTERFACE HID_HOST_DIAGNOSTICS=1)
endif()
if(MOUSE_PIPELINE_TRACE)
    target_sources(remapper_dual_b PRIVATE src/mouse_pipeline_trace.cc)
    target_compile_definitions(remapper_dual_b PRIVATE
        MOUSE_PIPELINE_TRACE=1 MOUSE_PIPELINE_TRACE_SOURCE=1 MOUSE_PIPELINE_TRACE_CAPACITY=256)
endif()
target_link_libraries(remapper_dual_b
    pico_stdlib
    tinyusb_host
    tinyusb_board
    usb_midi_host
)
pico_add_extra_outputs(remapper_dual_b)

set(DUAL_B_BINARY_OVERRIDE "" CACHE FILEPATH
    "Optional historical B HEX embedded in dual A for a diagnostic build")
if(DUAL_B_BINARY_OVERRIDE)
    add_custom_command(
        OUTPUT dual_b_binary.c dual_b_binary.h
        DEPENDS ${DUAL_B_BINARY_OVERRIDE}
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/../../../tools/hex_to_c_array.py
            ${DUAL_B_BINARY_OVERRIDE} dual_b_binary.c dual_b_binary
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT dual_b_binary.c dual_b_binary.h
        DEPENDS remapper_dual_b
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_LIST_DIR}/../../../tools/hex_to_c_array.py
            remapper_dual_b.hex dual_b_binary.c dual_b_binary
        VERBATIM
    )
endif()

add_executable(remapper_serial
    src/main.cc
    src/remapper.cc
    src/remapper_serial.cc
    src/crc.cc
    src/descriptor_parser.cc
    src/tinyusb_stuff.cc
    src/our_descriptor.cc
    src/globals.cc
    src/config.cc
    src/quirks.cc
    src/interval_override.cc
    src/tick.cc
    src/activity_led.cc
    src/ps_auth.cc
)
target_include_directories(remapper_serial PRIVATE
    src
    src/tusb_config_device
)
target_link_libraries(remapper_serial
    pico_stdlib
    pico_unique_id
    hardware_flash
    tinyusb_device
    tinyusb_board
)
pico_add_extra_outputs(remapper_serial)

add_executable(flash_b_side
    src/flash_b_side.cc
    dual_b_binary.c
    src/pico_debug/swd.c
    src/pico_debug/flash.c
    src/pico_debug/adi.c
)
target_include_directories(flash_b_side PRIVATE
    src
    src/pico_debug
)
target_link_libraries(flash_b_side
    pico_stdlib
    hardware_pio
)
pico_generate_pio_header(flash_b_side ${CMAKE_CURRENT_LIST_DIR}/src/pico_debug/swd.pio)
pico_add_extra_outputs(flash_b_side)
pico_set_binary_type(flash_b_side no_flash)

add_custom_command(
    OUTPUT remapper_dual_combined.uf2
    DEPENDS remapper_dual_a flash_b_side
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/combine_uf2.py remapper_dual_a.uf2 flash_b_side.uf2 remapper_dual_combined.uf2
    VERBATIM
)

add_custom_target(remapper_dual_combined ALL
    DEPENDS remapper_dual_combined.uf2
)
