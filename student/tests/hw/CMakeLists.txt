# Suíte da camada hw/: testa os módulos de hardware com drivers emulados do
# Zephyr (gpio-emul), sem falsos do FFF. Fica separada de tests/unit porque lá
# o FFF substitui funções de hw/, e as duas versões não podem coexistir num
# mesmo binário. O curso encontra esta suíte por estar em student/tests/<suíte>/.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_student_hw)

# hw/indicator: uma saída liga/desliga.
target_include_directories(app PRIVATE ../../src/hw/include)
target_sources(app PRIVATE
	indicator/test_indicator.c
	../../src/hw/src/indicator.c
)

# hw/motor: enable + direção (reaproveita hw/indicator).
target_sources(app PRIVATE
	motor/test_motor.c
	../../src/hw/src/motor.c
)

# hw/sensor: entrada com interrupção nas duas bordas.
target_sources(app PRIVATE
	sensor/test_sensor.c
	../../src/hw/src/sensor.c
)
# Não inclua main.c: a suíte tem o próprio ponto de entrada (ztest).
