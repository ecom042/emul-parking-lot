# hw/: one kind of hardware component per module, no parking rules; only
# Zephyr drivers are called here. Sources join the `app` target, like fsm/.
target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
	src/indicator.c
	src/lane_hw.c
	src/motor.c
	src/sensor.c
)
