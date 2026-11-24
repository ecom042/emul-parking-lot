cmake_minimum_required(VERSION 3.20.0)
# The lab's board (hardware/boards/ufal/parking_lot_u5) and course bindings.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_student)
# Todo .c de src/ entra no firmware: módulo novo não exige editar este arquivo.
file(GLOB app_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
target_sources(app PRIVATE ${app_sources})
