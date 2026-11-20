# services/: parking rules or a combination of hardware components; they use
# hw/. Sources join the `app` target, like fsm/.
target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
	src/signals.c
)
