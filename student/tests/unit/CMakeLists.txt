# Suíte de testes de unidade (ztest + FFF), compilada para a mesma placa e
# executada no Renode por scripts/firmware-coverage.py. As linhas e branches
# de student/src que ela executa contam para a cobertura se a suíte passar.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_student_unit)

target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE
  src/test_controller.c
  ../../src/controller.c
  ../../src/lane.c
  ../../src/adc.c
  ../../src/camera.c
  ../../src/payment.c
  ../../src/session.c
  ../../src/display.c
  ../../src/font.c
)
