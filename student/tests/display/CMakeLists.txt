cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_display_unit)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE src/test_display.c ../../src/payment_display.c
	../../src/parking_tariff.c ../../src/camera.c)
