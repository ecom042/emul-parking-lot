# Suite ztest + FFF da camada de UART (hw_uart.c). Compila o hw_uart.c real
# com um header forcado (src/shim.h) que redireciona as chamadas de driver da
# UART para falsos do FFF; os analisadores at_parser.c e p1_parser.c sao os
# reais, para que os quadros entregues por hw_uart_poll_* sejam verdadeiros.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_hw_uart)
target_include_directories(app PRIVATE ../../src src)
target_sources(app PRIVATE src/fakes.c src/test_hw_uart.c)
target_sources(app PRIVATE
	../../src/hw_uart.c
	../../src/at_parser.c
	../../src/p1_parser.c
)
set_source_files_properties(../../src/hw_uart.c PROPERTIES
	COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/shim.h")
