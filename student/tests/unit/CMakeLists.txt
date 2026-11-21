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
target_include_directories(app PRIVATE ../../src/include)
target_sources(app PRIVATE
	fsm/test_lane.c
	../../src/fsm/src/lane.c
)

# fsm/lanes: as duas faixas, suas filas e events_post().
target_sources(app PRIVATE
	fsm/test_lanes.c
	../../src/fsm/src/lanes.c
)

# services/signals: luzes e buzzer. hw/indicator é falsificado pelo FFF no
# teste; hw/lane_hw é o real (só dados do devicetree).
target_sources(app PRIVATE
	signals/test_signals.c
	../../src/services/src/signals.c
	../../src/hw/src/lane_hw.c
)

# services/barrier: motor + fins de curso. hw/motor e hw/sensor são falsos do
# FFF; events_post() é o real (fsm/lanes.c), lido pela fila da faixa.
target_sources(app PRIVATE
	barrier/test_barrier.c
	../../src/services/src/barrier.c
)

# Lógica pura dos protocolos (sem hardware, sem falsos): wire, line_framer,
# camera_proto, p1_proto e text.
target_sources(app PRIVATE
	wire/test_wire.c
	line_framer/test_line_framer.c
	camera_proto/test_camera_proto.c
	p1_proto/test_p1_proto.c
	text/test_text.c
	../../src/services/src/wire.c
	../../src/services/src/line_framer.c
	../../src/services/src/camera_proto.c
	../../src/services/src/p1_proto.c
	../../src/services/src/text.c
)
# hw/lane_hw: equipamento de cada faixa vindo do devicetree (lane_hw.c já está
# na lista do signals acima).
target_sources(app PRIVATE lane_hw/test_lane_hw.c)
# Não inclua main.c: a suíte tem o próprio ponto de entrada (ztest).
