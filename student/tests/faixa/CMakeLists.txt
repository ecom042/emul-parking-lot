# Suíte "faixa": a máquina de estados (lane.c) com hw.h e ui.h falsos.
# Sessões, tarifa e protocolos são os reais.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_unit_faixa)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE
  src/test_lane.c
  ../../src/lane.c
  ../../src/sessions.c
  ../../src/tariff.c
  ../../src/protocol.c
)
