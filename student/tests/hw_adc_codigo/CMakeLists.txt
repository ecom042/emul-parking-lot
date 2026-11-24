# Suite ztest + FFF de hw_adc.c, uma trava por boot. Compila o hw_adc.c real com
# src/shim.h forcado no inicio: o shim inclui os headers reais do Zephyr e
# redireciona device_is_ready, adc_channel_setup e adc_read_async para falsos
# do FFF (src/fakes.c). O kernel (k_poll_signal) continua o real.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_hw_adc_codigo)
set(HW_ADC_C ${CMAKE_CURRENT_SOURCE_DIR}/../../src/hw_adc.c)
target_include_directories(app PRIVATE ../../src src)
target_sources(app PRIVATE src/fakes.c src/test_hw_adc_codigo.c ${HW_ADC_C})
set_source_files_properties(${HW_ADC_C} PROPERTIES
	COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/shim.h")
