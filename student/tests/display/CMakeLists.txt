# Suíte do display (display.c + font.c) com a SPI falsa: sequência de
# inicialização, janelas e os caminhos de erro do barramento.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_display)
target_include_directories(app PRIVATE ../../src ../common)
target_sources(app PRIVATE
	src/test_display.c
	../common/hw_fake.c
	../../src/display.c
	../../src/font.c
	../../src/tariff.c
)
