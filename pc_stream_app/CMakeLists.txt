cmake_minimum_required(VERSION 3.20)
project(ht306_desktop_app C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ==============================================================================
# Target: ht306_desktop_app (Native C++ WebView2 Desktop Application)
# ==============================================================================
set(DESKTOP_APP_SOURCES
    src/main_webview.cpp
    src/app_state.cpp
    src/audio_capture.cpp
    src/usb_transport.cpp
    src/audio_endpoint_helper.cpp
    src/audio_policy_manager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/src/lossless_codec.c
)

add_executable(ht306_desktop_app WIN32
    ${DESKTOP_APP_SOURCES}
)

set_property(TARGET ht306_desktop_app PROPERTY CXX_STANDARD 17)
set_property(TARGET ht306_desktop_app PROPERTY CXX_STANDARD_REQUIRED ON)

target_include_directories(ht306_desktop_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/build/native/include
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/include
)

target_compile_definitions(ht306_desktop_app PRIVATE
    WEBVIEW_BUILD=1
    WEBVIEW_MSWEBVIEW2_BUILTIN_IMPL=1
)

target_link_libraries(ht306_desktop_app PRIVATE
    ole32
    oleaut32
    shlwapi
    version
    dwmapi
    winmm
    avrt
    setupapi
    winusb
    shell32
    ksuser
    uuid
)

if(MSVC)
    target_compile_options(ht306_desktop_app PRIVATE /O2 /W3)
else()
    target_compile_options(ht306_desktop_app PRIVATE -O3 -Wall -Wno-unused-variable)
    target_link_options(ht306_desktop_app PRIVATE -mwindows -static-libgcc -static-libstdc++)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ui)
    add_custom_command(TARGET ht306_desktop_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/ui
            $<TARGET_FILE_DIR:ht306_desktop_app>/ui
    )
endif()
