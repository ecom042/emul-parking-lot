#include <errno.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "parking_hw.h"
#include "services.h"

DEFINE_FFF_GLOBALS;

/* Drivers and lane logic are replaced; only the controller glue of main.c runs. */
FAKE_VOID_FUNC(parking_gpio_init, struct k_sem *);
FAKE_VOID_FUNC(parking_gpio_sample, enum parking_lane_id, struct parking_inputs *);
FAKE_VOID_FUNC(parking_adc_init, struct k_sem *);
FAKE_VOID_FUNC(parking_adc_sample, enum parking_lane_id, struct parking_inputs *);
FAKE_VOID_FUNC(parking_lane_init, struct parking_lane *, enum parking_lane_id,
	       const struct parking_io *, int64_t);
FAKE_VOID_FUNC(parking_lane_receive, struct parking_lane *, const struct parking_reply *);
FAKE_VOID_FUNC(parking_lane_step, struct parking_lane *, const struct parking_inputs *,
	       const struct parking_services *, int64_t);
FAKE_VALUE_FUNC(int, parking_services_start);
FAKE_VALUE_FUNC(int, parking_exit_start);
FAKE_VALUE_FUNC(int, fake_request, const struct parking_request *);
FAKE_VALUE_FUNC(int, fake_passage, enum parking_lane_id, uint32_t, int64_t);
FAKE_VOID_FUNC(fake_cancel, enum parking_lane_id, uint32_t);

const struct parking_io parking_gpio_io = {0};

/* main() never returns; rename it so the ztest runner keeps its own entry point. */
#define main parking_main
#include "../../../src/main.c"
#undef main

static const struct parking_services complete = {
	.request = fake_request,
	.passage = fake_passage,
	.cancel = fake_cancel,
};

static struct parking_reply received[REPLY_CAPACITY + 1];

static void record_reply(struct parking_lane *lane, const struct parking_reply *reply)
{
	ARG_UNUSED(lane);
	zassert_true(parking_lane_receive_fake.call_count <= ARRAY_SIZE(received));
	received[parking_lane_receive_fake.call_count - 1] = *reply;
}

static void reset_main(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(parking_gpio_sample);
	RESET_FAKE(parking_adc_sample);
	RESET_FAKE(parking_lane_receive);
	RESET_FAKE(parking_lane_step);
	FFF_RESET_HISTORY();
	parking_lane_receive_fake.custom_fake = record_reply;
	registered_services = NULL;
	k_msgq_purge(&replies);
	k_sem_reset(&controller_wake);
	memset(received, 0, sizeof(received));
}

static struct parking_reply reply_for(enum parking_lane_id lane, uint32_t transaction)
{
	return (struct parking_reply){
		.kind = PARKING_NO_PLATE,
		.lane = lane,
		.transaction = transaction,
		.attempt = 1,
	};
}

ZTEST(parking_main, test_register_rejects_incomplete_tables)
{
	struct parking_services missing_request = complete;
	struct parking_services missing_passage = complete;
	struct parking_services missing_cancel = complete;

	missing_request.request = NULL;
	missing_passage.passage = NULL;
	missing_cancel.cancel = NULL;

	zassert_equal(parking_register_services(NULL), -EINVAL);
	zassert_equal(parking_register_services(&missing_request), -EINVAL);
	zassert_equal(parking_register_services(&missing_passage), -EINVAL);
	zassert_equal(parking_register_services(&missing_cancel), -EINVAL);
	zassert_is_null(registered_services);
	zassert_equal(k_sem_count_get(&controller_wake), 0, "rejection must not wake the controller");
}

ZTEST(parking_main, test_register_accepts_once_and_keeps_first_table)
{
	struct parking_services other = complete;

	zassert_equal(parking_register_services(&complete), 0);
	zassert_equal_ptr(registered_services, &complete);
	zassert_equal(k_sem_count_get(&controller_wake), 1);

	zassert_equal(parking_register_services(&other), -EALREADY);
	zassert_equal_ptr(registered_services, &complete, "a second table must not replace the first");
}

ZTEST(parking_main, test_post_rejects_null_and_unknown_lane)
{
	struct parking_reply bad_lane = reply_for(PARKING_LANE_COUNT, 7);

	zassert_equal(parking_post_reply(NULL), -EINVAL);
	zassert_equal(parking_post_reply(&bad_lane), -EINVAL);
	zassert_equal(k_msgq_num_used_get(&replies), 0);
	zassert_equal(k_sem_count_get(&controller_wake), 0);
}

ZTEST(parking_main, test_post_reports_full_inbox_without_blocking)
{
	for (uint32_t i = 1; i <= REPLY_CAPACITY; i++) {
		struct parking_reply reply = reply_for(PARKING_ENTRY, i);

		zassert_equal(parking_post_reply(&reply), 0);
	}
	struct parking_reply extra = reply_for(PARKING_EXIT, 99);

	zassert_equal(parking_post_reply(&extra), -ENOMSG, "the service worker retries on -ENOMSG");
	zassert_equal(k_msgq_num_used_get(&replies), REPLY_CAPACITY);
	zassert_equal(k_sem_count_get(&controller_wake), 1);
}

ZTEST(parking_main, test_service_routes_replies_and_steps_both_lanes)
{
	struct parking_reply entry = reply_for(PARKING_ENTRY, 3);
	struct parking_reply exit = reply_for(PARKING_EXIT, 4);

	zassert_equal(parking_register_services(&complete), 0);
	zassert_equal(parking_post_reply(&entry), 0);
	zassert_equal(parking_post_reply(&exit), 0);

	service_lanes();

	zassert_equal(parking_lane_receive_fake.call_count, 2);
	zassert_equal_ptr(parking_lane_receive_fake.arg0_history[0], &lanes[PARKING_ENTRY]);
	zassert_equal_ptr(parking_lane_receive_fake.arg0_history[1], &lanes[PARKING_EXIT]);
	zassert_equal(received[0].transaction, 3);
	zassert_equal(received[1].transaction, 4);

	zassert_equal(parking_gpio_sample_fake.call_count, PARKING_LANE_COUNT);
	zassert_equal(parking_adc_sample_fake.call_count, PARKING_LANE_COUNT);
	zassert_equal(parking_lane_step_fake.call_count, PARKING_LANE_COUNT);
	zassert_equal_ptr(parking_lane_step_fake.arg0_history[0], &lanes[PARKING_ENTRY]);
	zassert_equal_ptr(parking_lane_step_fake.arg0_history[1], &lanes[PARKING_EXIT]);
	zassert_equal_ptr(parking_lane_step_fake.arg2_history[0], &complete);
	zassert_equal_ptr(parking_lane_step_fake.arg2_history[1], &complete);
}

ZTEST(parking_main, test_service_without_registration_still_steps_lanes)
{
	service_lanes();

	zassert_equal(parking_lane_receive_fake.call_count, 0);
	zassert_equal(parking_lane_step_fake.call_count, PARKING_LANE_COUNT,
		      "startup safety checks run before services exist");
	zassert_is_null(parking_lane_step_fake.arg2_history[0]);
	zassert_is_null(parking_lane_step_fake.arg2_history[1]);
}

ZTEST(parking_main, test_service_bounds_reply_work_per_wake)
{
	for (uint32_t i = 1; i <= REPLY_CAPACITY; i++) {
		struct parking_reply reply = reply_for(PARKING_EXIT, i);

		zassert_equal(parking_post_reply(&reply), 0);
	}

	service_lanes();

	zassert_equal(parking_lane_receive_fake.call_count, REPLY_CAPACITY);
	for (uint32_t i = 0; i < REPLY_CAPACITY; i++) {
		zassert_equal(received[i].transaction, i + 1, "replies keep FIFO order");
	}
	zassert_equal(k_msgq_num_used_get(&replies), 0);
	zassert_equal(parking_lane_step_fake.call_count, PARKING_LANE_COUNT,
		      "the lanes are stepped after the bounded drain");
}

ZTEST_SUITE(parking_main, NULL, NULL, reset_main, NULL, NULL);
