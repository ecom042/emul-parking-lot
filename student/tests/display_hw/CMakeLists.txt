# Suíte do driver REAL: display_hw.c fala com a SPI1 e os GPIOs simulados no Renode.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_display_hw_test)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE src/test_display_hw.c ../../src/display_hw.c)
