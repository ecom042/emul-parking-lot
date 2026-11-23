cmake_minimum_required(VERSION 3.20.0)
# The lab's board (hardware/boards/ufal/parking_lot_u5) and course bindings.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_student)
target_sources(app PRIVATE
	src/main.c
	# --- lógica de identificação, cobrança e tela (A) ---
	src/at_parser.c
	src/p1_parser.c
	src/tariff.c
	src/sessions.c
	src/uuid.c
	src/hw_uart.c
	src/st7789.c
	src/font.c
	# --- movimento e segurança (B) ---
	src/lane_cfg.c
	src/hw_io.c
	src/hw_adc.c
	src/presence.c
	src/lane.c
	src/controller.c
)
