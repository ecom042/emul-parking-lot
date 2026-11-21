# Suíte da camada fsm/: as regras das faixas (fsm/states) aplicadas pelo motor
# real (fsm/lane), com falsos do FFF no lugar dos serviços. Fica separada de
# tests/unit porque lá barrier.c, signals.c e os outros serviços são compilados
# de verdade, e os falsos com os mesmos nomes colidiriam no mesmo binário.
# O curso encontra esta suíte por estar em student/tests/<suíte>/.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_student_fsm)

target_include_directories(app PRIVATE ../../src/include)

# Falsos do FFF de todos os serviços que a fsm/ chama, num arquivo só (cada
# falso só pode ser definido uma vez por binário). Não listar os .c de
# services/.
target_sources(app PRIVATE fakes/service_fakes.c)

# fsm/states: regras da entrada, aplicadas pelo motor real (fsm/lane).
target_sources(app PRIVATE
	states/test_states.c
	../../src/fsm/src/lane.c
	../../src/fsm/src/states.c
)

# fsm/runner: partida das faixas e uma volta da thread. fsm/lanes é o real
# (filas e events_post), lido pelos testes.
target_sources(app PRIVATE
	runner/test_runner.c
	../../src/fsm/src/lanes.c
	../../src/fsm/src/runner.c
)
# Não inclua main.c: a suíte tem o próprio ponto de entrada (ztest).
