cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parking_lot)

target_sources(app PRIVATE
  src/main.c
  src/hw.c
  src/lane.c
  src/camera.c
  src/payment.c
  src/session.c
  src/display.c
  src/adc.c
  src/font.c
)
