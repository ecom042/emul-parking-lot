# Suíte da faixa (lane.c + barrier.c) com a camada de hardware falsa.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_lane)
target_include_directories(app PRIVATE ../../src ../common)
target_sources(app PRIVATE
	src/test_lane.c
	../common/hw_fake.c
	../../src/barrier.c
	../../src/lane.c
	../../src/line_parser.c
	../../src/camera.c
	../../src/sessions.c
	../../src/tariff.c
	../../src/payment.c
	../../src/display.c
	../../src/font.c
)
