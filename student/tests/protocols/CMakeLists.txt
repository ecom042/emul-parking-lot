# Câmera UART, tabela de sessões e serviços de parking.h (ztest + FFF).
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_protocols_unit)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE src/test_camera.c src/test_sessions.c src/test_services.c
	src/test_uart_rx.c ../../src/camera.c ../../src/parking_uart_rx.c
	../../src/payment.c ../../src/sessions.c ../../src/services.c)
