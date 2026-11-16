# Suite de testes de unidade (ztest + FFF), compilada para a mesma placa e
# executada no Renode por scripts/firmware-coverage.py. As linhas de
# student/src que ela executa contam para a cobertura se a suíte passar.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_student_unit)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE src/test_exemplo.c)
# Acrescente os módulos de student/src que esta suíte testa, por exemplo:
# target_sources(app PRIVATE ../../src/tarifa.c)
# Não inclua main.c: a suíte tem o próprio ponto de entrada (ztest).
