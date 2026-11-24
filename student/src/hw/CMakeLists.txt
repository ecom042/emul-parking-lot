# hw/: one kind of hardware component per module, no parking rules; only
# Zephyr drivers are called here. Sources join the `app` target, like fsm/.
target_sources(app PRIVATE
	src/adc4.c
	src/indicator.c
	src/lane_hw.c
	src/motor.c
	src/sensor.c
	src/st7789.c
	src/uart_line.c
)
