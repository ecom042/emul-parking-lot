# Suite ztest + FFF da thread de controle (controller.c). Compila o
# controller.c real com a logica real das faixas (lane.c e os modulos puros
# que ela usa); a camada de hardware (hw.h, hw_adc.h, hw_uart.h e st7789.h)
# e substituida por falsos do FFF em src/fakes.c. O kernel (k_sleep,
# k_uptime_get, printk) e o real.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_controller)
target_include_directories(app PRIVATE ../../src src)
target_sources(app PRIVATE src/fakes.c src/test_controller.c)
target_sources(app PRIVATE
	../../src/controller.c
	../../src/lane.c
	../../src/lane_cfg.c
	../../src/presence.c
	../../src/sessions.c
	../../src/tariff.c
	../../src/uuid.c
)
