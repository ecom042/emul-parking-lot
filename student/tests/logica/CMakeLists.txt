# Suíte "logica": módulos puros (tarifa, sessões, montador de linhas,
# protocolos, fonte) e os que só usam hw.h (display, ADC), com hw.h falso.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_unit_logica)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE
  src/fakes.c
  src/test_tariff.c
  src/test_sessions.c
  src/test_line_parser.c
  src/test_protocol.c
  src/test_display.c
  src/test_adc_sampler.c
  ../../src/tariff.c
  ../../src/sessions.c
  ../../src/line_parser.c
  ../../src/protocol.c
  ../../src/font.c
  ../../src/display.c
  ../../src/adc_sampler.c
)
