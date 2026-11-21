# fsm/: the lane state machine. It calls services/ only, never hw/.
# Sources join the `app` target (not a separate library): the course coverage
# tool only reads the objects of `app` and only counts files under src/.
target_sources(app PRIVATE
	src/lane.c
	src/lanes.c
	src/runner.c
	src/states.c
)
