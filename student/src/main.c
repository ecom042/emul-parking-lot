#include <errno.h>

#include <zephyr/kernel.h>

#include "parking_hw.h"
#include "services.h"

enum { INPUT_SETTLE_MS = 20, SERVICE_INTERVAL_MS = 1, REPLY_CAPACITY = 8 };

K_SEM_DEFINE(controller_wake, 0, 1);
K_MSGQ_DEFINE(replies, sizeof(struct parking_reply), REPLY_CAPACITY, 1);

static struct parking_lane lanes[PARKING_LANE_COUNT];
static struct k_spinlock services_lock;
static const struct parking_services *registered_services;

int parking_register_services(const struct parking_services *services)
{
	if (services == NULL || services->request == NULL || services->passage == NULL ||
	    services->cancel == NULL) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&services_lock);
	int rc = registered_services == NULL ? 0 : -EALREADY;
	if (rc == 0) {
		registered_services = services;
	}
	k_spin_unlock(&services_lock, key);
	k_sem_give(&controller_wake);
	return rc;
}

int parking_post_reply(const struct parking_reply *reply)
{
	if (reply == NULL || (unsigned int)reply->lane >= PARKING_LANE_COUNT) {
		return -EINVAL;
	}
	int rc = k_msgq_put(&replies, reply, K_NO_WAIT);
	if (rc == 0) {
		k_sem_give(&controller_wake);
	}
	return rc;
}

static void service_lanes(void)
{
	k_spinlock_key_t key = k_spin_lock(&services_lock);
	const struct parking_services *services = registered_services;
	k_spin_unlock(&services_lock, key);

	/* Bound queue work per wake so a producer cannot starve the safety checks. */
	for (int i = 0; i < REPLY_CAPACITY; i++) {
		struct parking_reply reply;
		if (k_msgq_get(&replies, &reply, K_NO_WAIT) != 0) {
			break;
		}
		parking_lane_receive(&lanes[reply.lane], &reply);
	}
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		struct parking_inputs inputs = {0};

		parking_gpio_sample(i, &inputs);
		parking_adc_sample(i, &inputs);
		parking_lane_step(&lanes[i], &inputs, services, k_uptime_get());
	}
}

int main(void)
{
	parking_gpio_init(&controller_wake);
	/* The model re-drives its held input levels on the first 10 ms tick. */
	int64_t ready_ms = k_uptime_get() + INPUT_SETTLE_MS;
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		parking_lane_init(&lanes[i], i, &parking_gpio_io, ready_ms);
	}
	parking_adc_init(&controller_wake);
	/* Without services the lanes stay idle and never open; startup checks still run. */
	(void)parking_services_start();
	(void)parking_exit_start();
	while (true) {
		service_lanes();
		/* EXTI and asynchronous replies wake the controller ahead of this tick. */
		k_sem_take(&controller_wake, K_MSEC(SERVICE_INTERVAL_MS));
	}
	return 0;
}
