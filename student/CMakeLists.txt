cmake_minimum_required(VERSION 3.20.0)
# The lab's board (hardware/boards/ufal/parking_lot_u5) and course bindings.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_student)
target_sources(app PRIVATE src/main.c)
