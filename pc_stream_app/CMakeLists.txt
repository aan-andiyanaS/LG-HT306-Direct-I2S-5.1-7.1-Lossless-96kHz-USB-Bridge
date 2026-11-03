cmake_minimum_required(VERSION 3.20)
project(ht306_stream_app C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Include paths
include_directories(
    src
    vendor/imgui
    vendor/imgui/backends
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/include
)

set(IMGUI_SOURCES
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_win32.cpp
    vendor/imgui/backends/imgui_impl_dx11.cpp
)

set(APP_SOURCES
    src/main.cpp
    src/app_state.cpp
    src/ui_theme.cpp
    src/ui_sidebar.cpp
    src/ui_dashboard.cpp
    src/ui_mixer.cpp
    src/ui_routing.cpp
    src/ui_profiles.cpp
    src/ui_sweep_test.cpp
    src/ui_log.cpp
    src/ui_settings.cpp
    src/usb_transport.cpp
    src/audio_capture.cpp
    src/audio_endpoint_helper.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/src/lossless_codec.c
)

add_executable(ht306_stream_app WIN32
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
)

target_link_libraries(ht306_stream_app PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    winusb
    setupapi
    ole32
    oleaut32
    winmm
    avrt
    uuid
    ksuser
)

# Compiler optimization
if(MSVC)
    target_compile_options(ht306_stream_app PRIVATE /O2 /W3)
else()
    target_compile_options(ht306_stream_app PRIVATE -O3 -Wall -Wno-unused-variable)
    target_link_options(ht306_stream_app PRIVATE -mwindows -static-libgcc -static-libstdc++)
endif()

add_custom_command(TARGET ht306_stream_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:ht306_stream_app>/assets
)
