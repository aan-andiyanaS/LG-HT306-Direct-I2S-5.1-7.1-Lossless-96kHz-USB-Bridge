cmake_minimum_required(VERSION 3.20)

# Default target board: Waveshare RP2350-Zero (kompatibel penuh dengan pico2 / RP2350A)
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()

if(DEFINED ENV{PICO_SDK_PATH} OR DEFINED PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    else()
        include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    endif()

    project(ht306_rp2350_zero C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_CXX_STANDARD 17)

    pico_sdk_init()

    add_executable(ht306_rp2350_zero
        src/main.c
        src/lossless_codec.c
        src/i2s_engine.c
        src/ht306_control.c
        src/usb_descriptors.c
    )

    target_include_directories(ht306_rp2350_zero PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
    )

    pico_generate_pio_header(ht306_rp2350_zero
        ${CMAKE_CURRENT_LIST_DIR}/src/i2s_multi.pio
    )

    # Matikan stdio_usb bawaan agar tidak bentrok dengan TinyUSB Descriptor kustom kita
    pico_enable_stdio_usb(ht306_rp2350_zero 0)
    pico_enable_stdio_uart(ht306_rp2350_zero 0)

    target_link_libraries(ht306_rp2350_zero PRIVATE
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_dma
        hardware_clocks
        hardware_sync
        tinyusb_device
        tinyusb_board
    )

    # Hasilkan file ht306_rp2350_zero.uf2 siap Drag-and-Drop ke RP2350-Zero
    pico_add_extra_outputs(ht306_rp2350_zero)
else()
    # Mode Host Testing (Membangun Unit Test Bit-Perfect Codec & Engine di PC)
    project(ht306_host_tests C)
    set(CMAKE_C_STANDARD 11)

    enable_testing()

    add_executable(test_lossless_codec
        tests/test_lossless_codec.c
        src/lossless_codec.c
        src/i2s_engine.c
    )

    target_include_directories(test_lossless_codec PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
    )

    add_test(NAME VerifyLossless96kBitPerfect COMMAND test_lossless_codec)
endif()
