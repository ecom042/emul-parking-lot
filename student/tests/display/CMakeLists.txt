# Suíte de testes de unidade (ztest + FFF) do display: fonte, layout/renderização e a
# máquina de estados (display.c). A camada de hardware (display_hw.c) é trocada por
# falsos FFF que alimentam um "painel falso" com as regras do modelo ST7789V.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_display_test)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE src/test_display_render.c ../../src/font5x7.c ../../src/display_render.c)
target_sources(app PRIVATE src/test_display.c ../../src/display.c)
