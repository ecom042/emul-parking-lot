# Suíte do driver REAL: camera_hw.c conversa com as UARTs simuladas das câmeras
# no Renode (sem falsos). O modelo responde "OK\r\n" ao comando "AT\r\n".
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_camera_hw_test)
target_include_directories(app PRIVATE ../../include)
target_sources(app PRIVATE src/test_camera_hw.c ../../src/drivers/camera_hw.c)
