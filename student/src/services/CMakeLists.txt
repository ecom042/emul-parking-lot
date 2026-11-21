# services/: parking rules or a combination of hardware components; they use
# hw/. Sources join the `app` target, like fsm/.
target_sources(app PRIVATE
	src/barrier.c
	src/camera.c
	src/camera_proto.c
	src/distance.c
	src/line_framer.c
	src/p1_proto.c
	src/payment.c
	src/presence.c
	src/screen.c
	src/sessions.c
	src/signals.c
	src/text.c
	src/wire.c
)
