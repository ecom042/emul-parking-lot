#include <errno.h>

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "lane.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, fake_motor, enum parking_lane_id, enum parking_motor);
FAKE_VALUE_FUNC(int, fake_signal, enum parking_lane_id, bool);
FAKE_VALUE_FUNC(int, fake_buzzer, enum parking_lane_id, bool);
FAKE_VALUE_FUNC(int, fake_request, const struct parking_request *);
FAKE_VALUE_FUNC(int, fake_passage, enum parking_lane_id, uint32_t, int64_t);
FAKE_VOID_FUNC(fake_cancel, enum parking_lane_id, uint32_t);

static const struct parking_io io = {
	.motor = fake_motor, .signal = fake_signal, .buzzer = fake_buzzer,
};
static const struct parking_services services = {
	.request = fake_request, .passage = fake_passage, .cancel = fake_cancel,
};
static struct parking_lane lane;
static struct parking_inputs inputs;
static struct parking_request last_request;

static int remember_request(const struct parking_request *request)
{
	last_request = *request;
	return fake_request_fake.return_val;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(fake_motor);
	RESET_FAKE(fake_signal);
	RESET_FAKE(fake_buzzer);
	RESET_FAKE(fake_request);
	RESET_FAKE(fake_passage);
	RESET_FAKE(fake_cancel);
	FFF_RESET_HISTORY();
	fake_request_fake.custom_fake = remember_request;
	last_request = (struct parking_request){0};
	inputs = (struct parking_inputs){ .lower = true, .distance = { .valid = true } };
	parking_lane_init(&lane, PARKING_ENTRY, &io, 20);
}

static void step(int64_t now_ms, uint16_t raw)
{
	inputs.distance.raw = raw;
	inputs.distance.sampled_ms = now_ms;
	parking_lane_step(&lane, &inputs, &services, now_ms);
}

static void reply(enum parking_reply_kind kind, uint32_t transaction, unsigned int attempt)
{
	const struct parking_reply message = {
		.kind = kind, .lane = lane.id, .transaction = transaction,
		.attempt = attempt, .plate = "ABC1D23",
	};
	parking_lane_receive(&lane, &message);
}

static void begin_open(enum parking_reply_kind allowed)
{
	step(20, 682);
	reply(PARKING_PLATE, last_request.transaction, 1);
	step(100, 682);
	reply(allowed, last_request.transaction, 0);
	step(200, 682);
	zassert_equal(lane.state, PARKING_OPENING);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_OPEN);
}

static void reach_upper(void)
{
	inputs.lower = false;
	inputs.upper = true;
	FFF_RESET_HISTORY();
	step(2200, 682);
	zassert_equal(lane.state, PARKING_PASSAGE);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_true(fake_signal_fake.arg1_val);
	zassert_true(fff.call_history[0] == (fff_function_t)fake_motor);
	zassert_true(fff.call_history[1] == (fff_function_t)fake_signal);
}

static void clear_passage(void)
{
	inputs.beam = true;
	step(2300, 1025);
	zassert_equal(fake_passage_fake.call_count, 0);
	inputs.beam = false;
	step(2400, 1024);
	zassert_equal(fake_passage_fake.call_count, 0);
	step(2500, 1025);
}

ZTEST_SUITE(parking_lane, NULL, NULL, before_each, NULL, NULL);

ZTEST(parking_lane, test_startup_without_lower_does_not_home)
{
	inputs.lower = false;
	step(19, 600);
	zassert_equal(lane.state, PARKING_STARTUP);
	zassert_equal(fake_motor_fake.call_count, 0);
	step(20, 600);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.latched);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_false(fake_signal_fake.arg1_val);
	zassert_true(fake_buzzer_fake.arg1_val);
	step(119, 1025);
	zassert_true(fake_buzzer_fake.arg1_val);
	step(120, 1025);
	zassert_false(fake_buzzer_fake.arg1_val);
	zassert_equal(fake_buzzer_fake.call_count, 2);
	zassert_equal(fake_request_fake.call_count, 0);
}

ZTEST(parking_lane, test_contradiction_latches_only_affected_lane)
{
	inputs.upper = true;
	step(20, 600);
	inputs.upper = false;
	step(150, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.latched);
	zassert_equal(fake_buzzer_fake.call_count, 2);

	struct parking_lane other;
	parking_lane_init(&other, PARKING_EXIT, &io, 20);
	inputs.distance.raw = 682;
	parking_lane_step(&other, &inputs, &services, 151);
	zassert_equal(other.state, PARKING_IDENTIFYING);
	zassert_equal(last_request.lane, PARKING_EXIT);
	zassert_equal(fake_motor_fake.call_count, 1);
}

ZTEST(parking_lane, test_no_services_cannot_authorize)
{
	inputs.distance.raw = 682;
	inputs.distance.sampled_ms = 20;
	parking_lane_step(&lane, &inputs, NULL, 20);
	zassert_equal(lane.state, PARKING_IDLE);
	zassert_equal(fake_motor_fake.call_count, 0);
	zassert_equal(fake_request_fake.call_count, 0);
}

ZTEST(parking_lane, test_presence_and_capture_deadlines)
{
	step(20, 683);
	zassert_equal(lane.state, PARKING_IDLE);
	inputs.distance.raw = 682;
	parking_lane_step(&lane, &inputs, &services, 120);
	zassert_equal(fake_request_fake.call_count, 0);
	step(121, 682);
	uint32_t transaction = last_request.transaction;
	zassert_equal(last_request.deadline_ms, 1121);
	reply(PARKING_NO_PLATE, transaction, 1);
	step(150, 700);
	zassert_equal(last_request.attempt, 2);
	reply(PARKING_PLATE, transaction, 1);
	step(1149, 700);
	zassert_equal(fake_request_fake.call_count, 2);
	step(1150, 700);
	zassert_equal(last_request.attempt, 3);
	step(2150, 700);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_request_fake.call_count, 3);
	zassert_equal(fake_cancel_fake.arg1_val, transaction);
	step(2250, 682);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	inputs.beam = true;
	step(2251, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	inputs.beam = false;
	step(2252, 1025);
	zassert_equal(lane.state, PARKING_IDLE);
	step(2253, 682);
	zassert_not_equal(last_request.transaction, transaction);
	zassert_equal(last_request.attempt, 1);
}

ZTEST(parking_lane, test_exit_requires_current_paid_and_fresh_distance)
{
	parking_lane_init(&lane, PARKING_EXIT, &io, 20);
	step(20, 682);
	uint32_t transaction = last_request.transaction;
	reply(PARKING_PLATE, transaction, 1);
	step(100, 682);
	zassert_equal(last_request.kind, PARKING_AUTHORIZE_EXIT);
	zassert_mem_equal(last_request.plate, "ABC1D23", PARKING_PLATE_SIZE);
	reply(PARKING_EXIT_PAID, transaction + 1, 0);
	reply(PARKING_ENTRY_ALLOWED, transaction, 0);
	step(150, 682);
	zassert_equal(fake_motor_fake.call_count, 0);
	reply(PARKING_EXIT_PAID, transaction, 0);
	parking_lane_step(&lane, &inputs, &services, 250);
	zassert_equal(fake_motor_fake.call_count, 0);
	step(251, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_cancel_fake.arg1_val, transaction);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_true(fake_buzzer_fake.arg1_val);
}

ZTEST(parking_lane, test_entry_commits_only_at_completed_passage)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	zassert_equal(last_request.kind, PARKING_AUTHORIZE_ENTRY);
	uint32_t transaction = last_request.transaction;
	reach_upper();
	step(2201, 1025);
	zassert_equal(fake_passage_fake.call_count, 0);
	clear_passage();
	zassert_equal(fake_passage_fake.call_count, 1);
	zassert_equal(fake_passage_fake.arg1_val, transaction);
	zassert_equal(fake_passage_fake.arg2_val, 2500);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_CLOSE);
	zassert_false(fake_signal_fake.arg1_val);
	inputs.upper = false;
	inputs.lower = true;
	step(4600, 1025);
	zassert_equal(lane.state, PARKING_IDLE);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_equal(fake_passage_fake.call_count, 1);
}

ZTEST(parking_lane, test_beam_racing_close_defers_enable_without_recommit)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	reach_upper();
	fake_motor_fake.return_val = -EAGAIN;
	clear_passage();
	zassert_equal(lane.state, PARKING_WAIT_CLEAR);
	unsigned int motor_calls = fake_motor_fake.call_count;
	inputs.beam = true;
	step(2600, 1025);
	zassert_equal(fake_motor_fake.call_count, motor_calls);
	inputs.beam = false;
	fake_motor_fake.return_val = 0;
	step(2700, 1025);
	zassert_equal(lane.state, PARKING_CLOSING);
	zassert_equal(fake_passage_fake.call_count, 1);
}

ZTEST(parking_lane, test_session_commit_failure_stops_cycle)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	reach_upper();
	fake_passage_fake.return_val = -EIO;
	clear_passage();
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_passage_fake.call_count, 1);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_false(fake_signal_fake.arg1_val);
	zassert_equal(fake_cancel_fake.arg1_val, last_request.transaction);
}

ZTEST(parking_lane, test_obstruction_edge_stops_closing_and_latches)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	reach_upper();
	clear_passage();
	inputs.upper = false;
	inputs.beam_rose = true;
	step(2650, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_false(fake_signal_fake.arg1_val);
	zassert_true(lane.latched);
	unsigned int motor_calls = fake_motor_fake.call_count;
	inputs.beam_rose = false;
	step(2750, 1025);
	zassert_equal(fake_motor_fake.call_count, motor_calls);
	zassert_equal(fake_buzzer_fake.call_count, 2);
}

ZTEST(parking_lane, test_jam_stops_at_motion_deadline)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	step(2699, 682);
	zassert_equal(lane.state, PARKING_OPENING);
	step(2700, 682);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.latched);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_false(fake_signal_fake.arg1_val);
}

ZTEST(parking_lane, test_missing_lower_latches_after_close_deadline)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	reach_upper();
	clear_passage();
	inputs.upper = false;
	step(4999, 1025);
	zassert_equal(lane.state, PARKING_CLOSING);
	step(5000, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	inputs.lower = true;
	step(5100, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
}

ZTEST(parking_lane, test_held_beam_times_out_without_session_or_close)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	reach_upper();
	inputs.beam = true;
	step(12199, 1025);
	zassert_equal(lane.state, PARKING_PASSAGE);
	step(12200, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_passage_fake.call_count, 0);
	zassert_equal(fake_motor_fake.call_count, 3);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
}

ZTEST(parking_lane, test_adc_fault_invalidates_authorized_motion)
{
	parking_lane_init(&lane, PARKING_EXIT, &io, 20);
	begin_open(PARKING_EXIT_PAID);
	inputs.adc_fault = true;
	step(300, 682);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_equal(fake_cancel_fake.call_count, 1);
	inputs.adc_fault = false;
	step(400, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
}

ZTEST(parking_lane, test_motor_driver_error_prevents_green)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	inputs.lower = false;
	inputs.upper = true;
	fake_motor_fake.return_val = -EIO;
	step(2200, 682);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.safe_outputs_pending);
	zassert_equal(fake_signal_fake.call_count, 1);
	zassert_false(fake_signal_fake.arg1_val);
	fake_motor_fake.return_val = 0;
	step(2201, 682);
	zassert_false(lane.safe_outputs_pending);
	zassert_equal(fake_signal_fake.call_count, 2);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
}

ZTEST(parking_lane, test_capture_queue_failure_cancels_and_stays_red)
{
	fake_request_fake.return_val = -ENOMSG;
	step(20, 682);
	zassert_equal(fake_request_fake.call_count, 1);
	zassert_equal(last_request.kind, PARKING_CAPTURE);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_cancel_fake.arg1_val, last_request.transaction);
	zassert_equal(lane.transaction, 0);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_false(fake_signal_fake.arg1_val);
	zassert_true(fake_buzzer_fake.arg1_val);
}

ZTEST(parking_lane, test_authorization_queue_failure_cancels_without_opening)
{
	step(20, 682);
	uint32_t transaction = last_request.transaction;

	reply(PARKING_PLATE, transaction, 1);
	fake_request_fake.return_val = -ENOMSG;
	step(100, 682);
	zassert_equal(fake_request_fake.call_count, 2);
	zassert_equal(last_request.kind, PARKING_AUTHORIZE_ENTRY);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_cancel_fake.arg1_val, transaction);
	zassert_equal(fake_motor_fake.call_count, 1);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
}

ZTEST(parking_lane, test_only_first_matching_reply_is_used_before_deadline)
{
	step(20, 682);
	uint32_t transaction = last_request.transaction;
	struct parking_reply answer = {
		.kind = PARKING_PLATE, .lane = PARKING_EXIT, .transaction = transaction,
		.attempt = 1, .plate = "ABC1D23",
	};

	parking_lane_receive(&lane, &answer);
	answer.lane = PARKING_ENTRY;
	answer.plate[PARKING_PLATE_SIZE - 1] = 'X';
	parking_lane_receive(&lane, &answer);
	reply(PARKING_DENIED, transaction, 1);
	step(50, 682);
	zassert_equal(lane.state, PARKING_IDENTIFYING);
	zassert_equal(fake_request_fake.call_count, 1);

	reply(PARKING_PLATE, transaction, 1);
	reply(PARKING_NO_PLATE, transaction, 1);
	step(100, 682);
	zassert_equal(lane.state, PARKING_AUTHORIZING);
	zassert_equal(last_request.kind, PARKING_AUTHORIZE_ENTRY);
	reply(PARKING_ENTRY_ALLOWED, transaction, 1);
	step(150, 682);
	zassert_equal(fake_motor_fake.call_count, 0);

	reply(PARKING_ENTRY_ALLOWED, transaction, 0);
	step(1100, 682);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_motor_fake.call_count, 1);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_equal(fake_cancel_fake.arg1_val, transaction);
}

ZTEST(parking_lane, test_invalid_and_future_distance_samples_do_not_start_capture)
{
	inputs.distance.valid = false;
	step(20, 682);
	zassert_equal(lane.state, PARKING_IDLE);
	zassert_equal(fake_request_fake.call_count, 0);

	inputs.distance.valid = true;
	inputs.distance.sampled_ms = 22;
	parking_lane_step(&lane, &inputs, &services, 21);
	zassert_equal(lane.state, PARKING_IDLE);
	zassert_equal(fake_request_fake.call_count, 0);

	parking_lane_step(&lane, &inputs, &services, 22);
	zassert_equal(lane.state, PARKING_IDENTIFYING);
	zassert_equal(fake_request_fake.call_count, 1);
}

ZTEST(parking_lane, test_open_command_error_immediately_orders_stop)
{
	step(20, 682);
	uint32_t transaction = last_request.transaction;
	reply(PARKING_PLATE, transaction, 1);
	step(100, 682);

	int motor_results[] = { -EIO, 0 };

	SET_RETURN_SEQ(fake_motor, motor_results, ARRAY_SIZE(motor_results));
	reply(PARKING_ENTRY_ALLOWED, transaction, 0);
	step(200, 682);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.latched);
	zassert_equal(fake_motor_fake.call_count, 2);
	zassert_equal(fake_motor_fake.arg1_history[0], PARKING_OPEN);
	zassert_equal(fake_motor_fake.arg1_history[1], PARKING_STOP);
	zassert_equal(fake_signal_fake.call_count, 1);
	zassert_false(fake_signal_fake.arg1_val);
}

ZTEST(parking_lane, test_failed_green_switch_cancels_passage)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	inputs.lower = false;
	inputs.upper = true;
	int signal_results[] = { -EIO, 0 };

	SET_RETURN_SEQ(fake_signal, signal_results, ARRAY_SIZE(signal_results));
	step(2200, 682);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.latched);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_equal(fake_signal_fake.call_count, 2);
	zassert_true(fake_signal_fake.arg1_history[0]);
	zassert_false(fake_signal_fake.arg1_history[1]);
	zassert_equal(fake_passage_fake.call_count, 0);
}

ZTEST(parking_lane, test_failed_red_switch_keeps_motor_off_and_retries_safe_outputs)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	reach_upper();
	fake_signal_fake.return_val = -EIO;
	clear_passage();
	zassert_equal(fake_passage_fake.call_count, 1);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.safe_outputs_pending);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_equal(fake_cancel_fake.call_count, 0);

	fake_signal_fake.return_val = 0;
	step(2501, 1025);
	zassert_false(lane.safe_outputs_pending);
	zassert_true(lane.latched);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_false(fake_signal_fake.arg1_val);
	zassert_equal(fake_passage_fake.call_count, 1);
}

ZTEST(parking_lane, test_buzzer_failure_latches_assistance_with_closed_barrier)
{
	step(20, 682);
	reply(PARKING_PLATE, last_request.transaction, 1);
	step(100, 682);

	int buzzer_results[] = { -EIO, -EIO, 0 };

	SET_RETURN_SEQ(fake_buzzer, buzzer_results, ARRAY_SIZE(buzzer_results));
	reply(PARKING_DENIED, last_request.transaction, 0);
	step(200, 682);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.latched);
	zassert_equal(fake_buzzer_fake.arg0_history[0], PARKING_ENTRY);
	zassert_true(fake_buzzer_fake.arg1_history[0]);

	step(300, 1025);
	zassert_true(lane.buzz_on);
	zassert_equal(fake_buzzer_fake.call_count, 2);
	step(301, 1025);
	zassert_false(lane.buzz_on);
	zassert_equal(fake_buzzer_fake.call_count, 3);
	zassert_false(fake_buzzer_fake.arg1_val);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
}

ZTEST(parking_lane, test_beam_reappearing_after_passage_does_not_extend_deadline)
{
	begin_open(PARKING_ENTRY_ALLOWED);
	reach_upper();
	fake_motor_fake.return_val = -EAGAIN;
	clear_passage();
	zassert_equal(lane.state, PARKING_WAIT_CLEAR);
	unsigned int motor_calls = fake_motor_fake.call_count;

	inputs.beam = true;
	fake_motor_fake.return_val = 0;
	step(12199, 1025);
	zassert_equal(lane.state, PARKING_WAIT_CLEAR);
	zassert_equal(fake_motor_fake.call_count, motor_calls);

	step(12200, 1025);
	zassert_equal(lane.state, PARKING_ASSISTANCE);
	zassert_true(lane.latched);
	zassert_equal(fake_motor_fake.call_count, motor_calls + 1);
	zassert_equal(fake_motor_fake.arg1_val, PARKING_STOP);
	zassert_equal(fake_passage_fake.call_count, 1);
}
