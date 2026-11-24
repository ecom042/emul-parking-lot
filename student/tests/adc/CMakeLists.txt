# Suíte do amostrador do ADC (adc_sampler.c) com a camada de hardware falsa.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_adc)
target_include_directories(app PRIVATE ../../src ../common)
target_sources(app PRIVATE
	src/test_adc.c
	../common/hw_fake.c
	../../src/adc_sampler.c
)
