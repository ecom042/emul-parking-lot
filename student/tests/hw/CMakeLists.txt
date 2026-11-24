# Suíte da camada de hardware real (hw.c) contra os modelos do Renode:
# GPIO, EXTI e ADC4 assíncrono de verdade, sem falsos.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_hw)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE
	src/test_hw.c
	../../src/hw.c
	../../src/display.c
	../../src/font.c
	../../src/tariff.c
)
