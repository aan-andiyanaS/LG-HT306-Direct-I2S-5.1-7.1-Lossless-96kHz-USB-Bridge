cmake_minimum_required(VERSION 3.20)
project(ht306_pc_streamer C)
set(CMAKE_C_STANDARD 11)

add_executable(ht306_pc_streamer
    src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/src/lossless_codec.c
)

target_include_directories(ht306_pc_streamer PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../firmware/include
)
