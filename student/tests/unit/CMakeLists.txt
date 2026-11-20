# Suite de testes de unidade (ztest + FFF), compilada para a mesma placa e
# executada no Renode por scripts/firmware-coverage.py. As linhas de
# student/src que ela executa contam para a cobertura se a suíte passar.
#
# É uma suíte só (o curso só procura student/tests/<suíte>/), com uma
# subpasta por módulo testado: fsm/, e depois as dos outros módulos.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_student_unit)

target_sources(app PRIVATE src/test_exemplo.c)

# fsm/lane: motor genérico da máquina de estados.
target_include_directories(app PRIVATE ../../src/fsm/include)
target_sources(app PRIVATE
	fsm/test_lane.c
	../../src/fsm/src/lane.c
)

# services/signals: luzes e buzzer. hw/indicator é falsificado pelo FFF no
# teste; hw/lane_hw é o real (só dados do devicetree).
target_include_directories(app PRIVATE
	../../src/services/include
	../../src/hw/include
)
target_sources(app PRIVATE
	signals/test_signals.c
	../../src/services/src/signals.c
	../../src/hw/src/lane_hw.c
)
# Não inclua main.c: a suíte tem o próprio ponto de entrada (ztest).
