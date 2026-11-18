cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_lot_unit_tests)

target_sources(app PRIVATE
  src/main.c
  ../../src/session.c
  ../../src/camera.c
  ../../src/payment.c
)

target_include_directories(app PRIVATE ../../src)
