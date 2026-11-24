#include "services/barrier.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "hw/motor.h"
#include "hw/sensor.h"
#include "services/events.h"

/* Per-lane state of the barrier. */
static struct barrier_lane {
	struct sensor upper;      /**< limit switch at 90° (open) */
	struct sensor lower;      /**< limit switch at 0° (closed) */
	struct k_work limit_work; /**< reacts to a limit change outside the ISR */
	enum lane_id id;          /**< lane this entry serves */
} self[LANE_COUNT];

static const struct motor *motor_of(enum lane_id id)
{
	return &lane_hw_get(id)->motor;
}

/* Stop first, then tell the lane: by the time the lane hears that the arm
 * arrived, the motor is already off (REQ-06: green only with motor off).
 */
static void stop_and_post(const struct barrier_lane *lane, enum lane_event event)
{
	/* A work item has no caller to report to; if the stop failed, the
	 * state machine's motion deadline still catches the moving motor.
	 */
	(void)motor_stop(motor_of(lane->id));
	(void)events_post(lane->id, event);
}

/* Runs in the system work queue after any limit switch change. It decides by
 * the current levels, not by which edge woke it: the arm can only reach the
 * limit in its direction of travel, so an active limit is the destination.
 */
static void on_limit_work(struct k_work *work)
{
	const struct barrier_lane *lane = CONTAINER_OF(work, struct barrier_lane, limit_work);
	bool upper = sensor_is_active(&lane->upper);
	bool lower = sensor_is_active(&lane->lower);

	if (upper && lower) {
		stop_and_post(lane, EV_LIMITS_CONFLICT); /* REQ-01 */
	} else if (upper) {
		stop_and_post(lane, EV_UPPER_REACHED);
	} else if (lower) {
		stop_and_post(lane, EV_LOWER_REACHED);
	}
	/* Neither active: the arm just left a limit; nothing to do. */
}

/* Sensor handler, in interrupt context: defer everything to the work item. */
static void on_limit_change(void *ctx, bool active)
{
	struct barrier_lane *lane = ctx;

	ARG_UNUSED(active);
	(void)k_work_submit(&lane->limit_work);
}

int barrier_init(enum lane_id id)
{
	const struct lane_hw *hw = lane_hw_get(id);
	struct barrier_lane *lane = &self[id];

	/* State first: the sensor interrupts may fire as soon as they are on. */
	lane->id = id;
	k_work_init(&lane->limit_work, on_limit_work);

	int err = motor_init(&hw->motor);

	if (err != 0) {
		return err;
	}
	err = sensor_init(&lane->upper, &hw->limit_upper, on_limit_change, lane);
	if (err != 0) {
		return err;
	}
	return sensor_init(&lane->lower, &hw->limit_lower, on_limit_change, lane);
}

int barrier_open(enum lane_id id)
{
	return motor_drive(motor_of(id), MOTOR_OPEN);
}

int barrier_close(enum lane_id id)
{
	return motor_drive(motor_of(id), MOTOR_CLOSE);
}

int barrier_stop(enum lane_id id)
{
	return motor_stop(motor_of(id));
}

bool barrier_is_closed(enum lane_id id)
{
	return sensor_is_active(&self[id].lower) && !sensor_is_active(&self[id].upper);
}
