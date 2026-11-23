# Suíte ztest + FFF da máquina de estados de uma faixa. Compila a lógica
# (lane.c e os módulos puros que ela usa); a camada de hardware (hw.h, UART e
# display) é substituída por falsos do FFF em src/fakes.c.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_lane)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE src/fakes.c src/test_lane.c)
target_sources(app PRIVATE
	../../src/lane.c
	../../src/lane_cfg.c
	../../src/presence.c
	../../src/sessions.c
	../../src/tariff.c
	../../src/uuid.c
)
