# Suíte de testes de unidade (ztest + FFF) do pagamento: tarifa, UUID, analisador
# P1 e máquina de cobrança. A camada de hardware (payment_hw.c) e o display são
# substituídos por falsos FFF dentro de src/test_payment.c.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_payment_test)
target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE src/test_tariff.c ../../src/tariff.c)
target_sources(app PRIVATE src/test_payment_parser.c ../../src/payment_parser.c ../../src/payment_uuid.c)
target_sources(app PRIVATE src/test_payment.c ../../src/payment.c)
