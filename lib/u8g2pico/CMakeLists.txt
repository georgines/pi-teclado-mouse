cmake_minimum_required(VERSION 3.13)

add_subdirectory(lib/u8g2)

file(GLOB U8G2PICO_SOURCES 
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/lib/u8g2/csrc/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/lib/u8g2/csrc/*.cpp"
)

add_library(u8g2pico ${U8G2PICO_SOURCES})

target_include_directories(u8g2pico PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/lib/u8g2/csrc
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(u8g2pico PUBLIC
        pico_stdlib
        hardware_timer
        hardware_i2c
        u8g2
)

