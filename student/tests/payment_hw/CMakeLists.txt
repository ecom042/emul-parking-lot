# Suíte do driver REAL: payment_hw.c conversa com a LPUART1 simulada do terminal
# no Renode (sem falsos).
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_payment_hw_test)
target_include_directories(app PRIVATE ../../include)
target_sources(app PRIVATE src/test_payment_hw.c ../../src/drivers/payment_hw.c)
