# Suite ztest + FFF da camada de GPIO das faixas (hw_io.c). Compila o hw_io.c
# real com src/shim.h incluido a forca: o shim inclui os headers do driver e
# redireciona as chamadas gpio_*_dt para falsos do FFF (src/fakes.c). O kernel
# (k_work, atomic) e o real.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_hw_io)
target_include_directories(app PRIVATE ../../src src)
target_sources(app PRIVATE src/fakes.c src/test_hw_io.c)
target_sources(app PRIVATE
	../../src/hw_io.c
	../../src/lane_cfg.c
)
set_source_files_properties(../../src/hw_io.c PROPERTIES
	COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/shim.h")
