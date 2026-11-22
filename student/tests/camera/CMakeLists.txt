# Suíte de testes de unidade (ztest + FFF) da câmera: analisador de linhas
# (camera_parser.c) e máquina de captura (camera.c).
# A camada de hardware (camera_hw.c) NÃO entra aqui: é substituída por falsos do
# FFF dentro de src/test_camera.c. Ela é testada na suíte ../camera_hw.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_camera_test)
target_include_directories(app PRIVATE ../../include)
target_sources(app PRIVATE src/test_camera_parser.c ../../src/camera_parser.c)
target_sources(app PRIVATE src/test_camera.c ../../src/camera.c)
