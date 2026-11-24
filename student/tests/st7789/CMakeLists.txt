# Suite ztest + FFF do display de pagamento (st7789.c) e da fonte (font.c).
# Compila o st7789.c real com src/shim.h incluido a forca: o shim inclui os
# headers dos drivers e redireciona spi_*_dt e gpio_*_dt para falsos do FFF
# (src/fakes.c), que registram o que passaria pelo barramento SPI. O kernel
# (k_msgq, k_thread, k_msleep) e o real.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_st7789)
target_include_directories(app PRIVATE ../../src src)
target_sources(app PRIVATE src/fakes.c src/test_st7789.c src/test_font.c)
target_sources(app PRIVATE
	../../src/st7789.c
	../../src/font.c
)
set_source_files_properties(../../src/st7789.c PROPERTIES
	COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/shim.h")
