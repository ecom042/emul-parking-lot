# Suíte dos módulos de protocolo e dados, sem hardware: montador de linhas,
# câmera, sessões, tarifa e pagamento.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_proto)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE
	src/test_parser.c
	src/test_camera.c
	src/test_sessions.c
	src/test_payment.c
	../../src/line_parser.c
	../../src/camera.c
	../../src/sessions.c
	../../src/tariff.c
	../../src/payment.c
)
