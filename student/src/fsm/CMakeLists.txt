# fsm/: the lane state machine. It calls services/ only, never hw/.
# Sources join the `app` target (not a separate library): the course coverage
# tool only reads the objects of `app` and only counts files under src/.
target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
	src/lane.c
	src/lanes.c
)
