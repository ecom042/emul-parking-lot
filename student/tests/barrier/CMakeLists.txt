# Suíte da cancela (barrier.c) com a camada de hardware falsa.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_barrier)
target_include_directories(app PRIVATE ../../src ../common)
target_sources(app PRIVATE
	src/test_barrier.c
	../common/hw_fake.c
	../../src/barrier.c
)
