# Suíte ztest de unidade. Compila só o módulo de lógica, nunca main.c nem hw_*.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_test_parsers)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE src/test_parsers.c)
target_sources(app PRIVATE ../../src/at_parser.c ../../src/p1_parser.c)
