#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(bool, gpio_ready, const struct gpio_dt_spec *);
FAKE_VALUE_FUNC(int, configure_pin, const struct gpio_dt_spec *, gpio_flags_t);
FAKE_VALUE_FUNC(int, configure_interrupt, const struct gpio_dt_spec *, gpio_flags_t);
FAKE_VALUE_FUNC(int, add_callback, const struct device *, struct gpio_callback *);
FAKE_VALUE_FUNC(int, read_pin, const struct gpio_dt_spec *);
FAKE_VALUE_FUNC(int, write_pin, const struct gpio_dt_spec *, int);

/* Test the same implementation and devicetree aliases as the firmware. */
#define gpio_is_ready_dt gpio_ready
#define gpio_pin_configure_dt configure_pin
#define gpio_pin_interrupt_configure_dt configure_interrupt
#define gpio_add_callback add_callback
#define gpio_pin_get_dt read_pin
#define gpio_pin_set_dt write_pin
#include "../../../src/parking_gpio.c"
#undef gpio_pin_set_dt
#undef gpio_pin_get_dt
#undef gpio_add_callback
#undef gpio_pin_interrupt_configure_dt
#undef gpio_pin_configure_dt
#undef gpio_is_ready_dt

static struct k_sem changed;
static int readings[3];

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(gpio_ready);
	RESET_FAKE(configure_pin);
	RESET_FAKE(configure_interrupt);
	RESET_FAKE(add_callback);
	RESET_FAKE(read_pin);
	RESET_FAKE(write_pin);
	FFF_RESET_HISTORY();
	gpio_ready_fake.return_val = true;
	zassert_ok(k_sem_init(&changed, 0, 1));
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		atomic_set(&pins[i].beam_rose, 0);
		pins[i].ready = false;
	}
}

static void input_levels(int upper, int lower, int beam)
{
	RESET_FAKE(read_pin);
	readings[0] = upper;
	readings[1] = lower;
	readings[2] = beam;
	SET_RETURN_SEQ(read_pin, readings, ARRAY_SIZE(readings));
}

static struct parking_inputs sample(enum parking_lane_id lane)
{
	struct parking_inputs inputs = {0};

	parking_gpio_sample(lane, &inputs);
	return inputs;
}

static void expect_configuration(size_t index, const struct gpio_dt_spec *pin, gpio_flags_t flags)
{
	zassert_equal_ptr(configure_pin_fake.arg0_history[index], pin);
	zassert_equal(configure_pin_fake.arg1_history[index], flags);
}

static void expect_write(size_t index, const struct gpio_dt_spec *pin, int value)
{
	zassert_equal_ptr(write_pin_fake.arg0_history[index], pin);
	zassert_equal(write_pin_fake.arg1_history[index], value);
}

static void expect_failed_initialization(void)
{
	struct parking_inputs entry = sample(PARKING_ENTRY);

	zassert_true(entry.gpio_fault);
	zassert_equal(read_pin_fake.call_count, 0, "an unconfigured lane must not read input");
	zassert_equal(parking_gpio_io.motor(PARKING_ENTRY, PARKING_OPEN), -EINVAL);
	zassert_equal(write_pin_fake.call_count, 0);
	struct parking_inputs exit = sample(PARKING_EXIT);

	zassert_false(exit.gpio_fault, "entry setup failure must not disable the other lane");
	zassert_equal(read_pin_fake.call_count, 3);
	zassert_ok(parking_gpio_io.motor(PARKING_ENTRY, PARKING_STOP));
	expect_write(0, &pins[PARKING_ENTRY].enable, 0);
}

ZTEST(parking_gpio, test_safe_startup_and_interrupt_configuration)
{
	parking_gpio_init(&changed);
	zassert_equal(configure_pin_fake.call_count, 16);
	zassert_equal(add_callback_fake.call_count, 4);
	zassert_equal(configure_interrupt_fake.call_count, 6);

	for (size_t i = 0; i < PARKING_LANE_COUNT; i++) {
		const struct lane_gpio *lane = &pins[i];
		size_t base = i * 8;

		expect_configuration(base, &lane->enable, GPIO_OUTPUT_INACTIVE);
		expect_configuration(base + 1, &lane->green, GPIO_OUTPUT_INACTIVE);
		expect_configuration(base + 2, &lane->red, GPIO_OUTPUT_ACTIVE);
		expect_configuration(base + 3, &lane->direction, GPIO_OUTPUT_INACTIVE);
		expect_configuration(base + 4, &lane->buzzer, GPIO_OUTPUT_INACTIVE);
		expect_configuration(base + 5, &lane->upper, GPIO_INPUT);
		expect_configuration(base + 6, &lane->lower, GPIO_INPUT);
		expect_configuration(base + 7, &lane->beam, GPIO_INPUT);
		zassert_equal_ptr(add_callback_fake.arg0_history[i * 2], lane->upper.port);
		zassert_equal_ptr(add_callback_fake.arg1_history[i * 2], &lane->limits_cb);
		zassert_equal(lane->limits_cb.pin_mask, BIT(lane->upper.pin) | BIT(lane->lower.pin));
		zassert_equal_ptr(add_callback_fake.arg0_history[i * 2 + 1], lane->beam.port);
		zassert_equal_ptr(add_callback_fake.arg1_history[i * 2 + 1], &lane->beam_cb);
		zassert_equal(lane->beam_cb.pin_mask, BIT(lane->beam.pin));
		zassert_equal_ptr(configure_interrupt_fake.arg0_history[i * 3], &lane->upper);
		zassert_equal(configure_interrupt_fake.arg1_history[i * 3], GPIO_INT_EDGE_BOTH);
		zassert_equal_ptr(configure_interrupt_fake.arg0_history[i * 3 + 1], &lane->lower);
		zassert_equal(configure_interrupt_fake.arg1_history[i * 3 + 1], GPIO_INT_EDGE_BOTH);
		zassert_equal_ptr(configure_interrupt_fake.arg0_history[i * 3 + 2], &lane->beam);
		zassert_equal(configure_interrupt_fake.arg1_history[i * 3 + 2], GPIO_INT_EDGE_TO_ACTIVE);
	}
}

ZTEST(parking_gpio, test_unready_enable_still_configures_safe_signal)
{
	bool ready[] = {false, true};

	SET_RETURN_SEQ(gpio_ready, ready, ARRAY_SIZE(ready));
	parking_gpio_init(&changed);
	zassert_equal(configure_pin_fake.call_count, 15);
	expect_configuration(0, &pins[PARKING_ENTRY].green, GPIO_OUTPUT_INACTIVE);
	expect_configuration(1, &pins[PARKING_ENTRY].red, GPIO_OUTPUT_ACTIVE);
	zassert_equal(add_callback_fake.call_count, 2);
	expect_failed_initialization();
}

ZTEST(parking_gpio, test_pin_configuration_error_leaves_lane_faulted)
{
	int responses[] = {-EIO, 0};

	SET_RETURN_SEQ(configure_pin, responses, ARRAY_SIZE(responses));
	parking_gpio_init(&changed);
	zassert_equal(configure_pin_fake.call_count, 16);
	zassert_equal(add_callback_fake.call_count, 2);
	expect_failed_initialization();
}

ZTEST(parking_gpio, test_callback_registration_errors_prevent_interrupt_enable)
{
	for (size_t failing_call = 0; failing_call < 2; failing_call++) {
		int responses[] = {0, 0, 0};

		before_each(NULL);
		responses[failing_call] = -EIO;
		SET_RETURN_SEQ(add_callback, responses, ARRAY_SIZE(responses));
		parking_gpio_init(&changed);
		zassert_equal(add_callback_fake.call_count, failing_call + 3);
		zassert_equal(configure_interrupt_fake.call_count, 3);
		zassert_equal_ptr(configure_interrupt_fake.arg0_history[0], &pins[PARKING_EXIT].upper);
		expect_failed_initialization();
	}
}

ZTEST(parking_gpio, test_each_interrupt_setup_error_faults_only_its_lane)
{
	for (size_t failing_call = 0; failing_call < 3; failing_call++) {
		int responses[] = {0, 0, 0, 0};

		before_each(NULL);
		responses[failing_call] = -EIO;
		SET_RETURN_SEQ(configure_interrupt, responses, ARRAY_SIZE(responses));
		parking_gpio_init(&changed);
		zassert_equal(configure_interrupt_fake.call_count, failing_call + 4);
		zassert_equal_ptr(configure_interrupt_fake.arg0_history[failing_call + 1],
				  &pins[PARKING_EXIT].upper);
		expect_failed_initialization();
	}
}

ZTEST(parking_gpio, test_limits_wake_and_beam_edge_is_consumed_once_in_its_lane)
{
	parking_gpio_init(&changed);
	struct gpio_callback *limit = add_callback_fake.arg1_history[0];
	struct gpio_callback *beam = add_callback_fake.arg1_history[1];

	limit->handler(add_callback_fake.arg0_history[0], limit, BIT(pins[PARKING_ENTRY].lower.pin));
	zassert_ok(k_sem_take(&changed, K_NO_WAIT));
	zassert_false(sample(PARKING_ENTRY).beam_rose, "limit edge is not a beam edge");

	beam->handler(add_callback_fake.arg0_history[1], beam, beam->pin_mask);
	zassert_ok(k_sem_take(&changed, K_NO_WAIT));
	zassert_false(sample(PARKING_EXIT).beam_rose, "beam edge must stay in its lane");
	input_levels(1, 0, 1);
	struct parking_inputs entry = sample(PARKING_ENTRY);

	zassert_true(entry.beam_rose);
	zassert_true(entry.upper);
	zassert_false(entry.lower);
	zassert_true(entry.beam);
	zassert_false(entry.gpio_fault);
	zassert_equal_ptr(read_pin_fake.arg0_history[0], &pins[PARKING_ENTRY].upper);
	zassert_equal_ptr(read_pin_fake.arg0_history[1], &pins[PARKING_ENTRY].lower);
	zassert_equal_ptr(read_pin_fake.arg0_history[2], &pins[PARKING_ENTRY].beam);
	zassert_false(sample(PARKING_ENTRY).beam_rose);
}

ZTEST(parking_gpio, test_each_input_error_is_reported)
{
	parking_gpio_init(&changed);
	input_levels(-EIO, 1, 0);
	struct parking_inputs entry = sample(PARKING_ENTRY);

	zassert_true(entry.gpio_fault);
	zassert_false(entry.upper);
	zassert_true(entry.lower);

	input_levels(1, -EIO, 0);
	struct parking_inputs exit = sample(PARKING_EXIT);

	zassert_true(exit.gpio_fault);
	zassert_true(exit.upper);
	zassert_false(exit.lower);

	input_levels(0, 1, -EIO);
	exit = sample(PARKING_EXIT);
	zassert_true(exit.gpio_fault);
	zassert_false(exit.beam);
	zassert_equal(read_pin_fake.call_count, 3);
}

ZTEST(parking_gpio, test_motor_open_close_and_stop_order)
{
	const struct lane_gpio *lane = &pins[PARKING_ENTRY];

	parking_gpio_init(&changed);
	input_levels(0, 1, 0);
	zassert_ok(parking_gpio_io.motor(PARKING_ENTRY, PARKING_OPEN));
	zassert_equal(read_pin_fake.call_count, 2);
	zassert_equal(write_pin_fake.call_count, 2);
	expect_write(0, &lane->direction, 1);
	expect_write(1, &lane->enable, 1);

	input_levels(1, 0, 0);
	FFF_RESET_HISTORY();
	zassert_ok(parking_gpio_io.motor(PARKING_ENTRY, PARKING_CLOSE));
	zassert_equal(read_pin_fake.call_count, 3);
	zassert_equal_ptr(read_pin_fake.arg0_history[2], &lane->beam);
	zassert_equal(fff.call_history_idx, 7);
	zassert_true(fff.call_history[3] == (fff_function_t)write_pin);
	zassert_true(fff.call_history[4] == (fff_function_t)read_pin);
	zassert_true(fff.call_history[6] == (fff_function_t)write_pin);
	expect_write(2, &lane->direction, 0);
	expect_write(3, &lane->enable, 1);

	zassert_ok(parking_gpio_io.motor(PARKING_ENTRY, PARKING_STOP));
	zassert_equal(read_pin_fake.call_count, 3, "stop must not depend on input readings");
	expect_write(4, &lane->enable, 0);
}

ZTEST(parking_gpio, test_motor_refuses_invalid_command_and_limits)
{
	parking_gpio_init(&changed);
	zassert_equal(parking_gpio_io.motor(PARKING_ENTRY, (enum parking_motor)99), -EINVAL);
	zassert_equal(read_pin_fake.call_count, 0);
	input_levels(1, 1, 0);
	zassert_equal(parking_gpio_io.motor(PARKING_ENTRY, PARKING_OPEN), -EIO);
	input_levels(1, 1, 0);
	zassert_equal(parking_gpio_io.motor(PARKING_ENTRY, PARKING_CLOSE), -EIO);
	zassert_equal(write_pin_fake.call_count, 0);
}

ZTEST(parking_gpio, test_close_rechecks_beam_before_enabling_motor)
{
	const struct lane_gpio *lane = &pins[PARKING_EXIT];

	parking_gpio_init(&changed);
	input_levels(1, 0, 1);
	zassert_equal(parking_gpio_io.motor(PARKING_EXIT, PARKING_CLOSE), -EAGAIN);
	zassert_equal(write_pin_fake.call_count, 1);
	expect_write(0, &lane->direction, 0);
	zassert_equal_ptr(read_pin_fake.arg0_history[2], &lane->beam);

	input_levels(1, 0, -EIO);
	zassert_equal(parking_gpio_io.motor(PARKING_EXIT, PARKING_CLOSE), -EIO);
	zassert_equal(write_pin_fake.call_count, 2);
	expect_write(1, &lane->direction, 0);
}

ZTEST(parking_gpio, test_motor_direction_and_enable_errors_are_returned)
{
	const struct lane_gpio *lane = &pins[PARKING_ENTRY];
	int responses[] = {0, -EIO};

	parking_gpio_init(&changed);
	input_levels(0, 1, 0);
	write_pin_fake.return_val = -EIO;
	zassert_equal(parking_gpio_io.motor(PARKING_ENTRY, PARKING_OPEN), -EIO);
	zassert_equal(write_pin_fake.call_count, 1);
	expect_write(0, &lane->direction, 1);

	RESET_FAKE(write_pin);
	input_levels(0, 1, 0);
	SET_RETURN_SEQ(write_pin, responses, ARRAY_SIZE(responses));
	zassert_equal(parking_gpio_io.motor(PARKING_ENTRY, PARKING_OPEN), -EIO);
	zassert_equal(write_pin_fake.call_count, 2);
	expect_write(1, &lane->enable, 1);
}

ZTEST(parking_gpio, test_green_requires_open_limit_and_successful_red_off)
{
	const struct lane_gpio *lane = &pins[PARKING_EXIT];

	parking_gpio_init(&changed);
	input_levels(0, 1, 0);
	zassert_equal(parking_gpio_io.signal(PARKING_EXIT, true), -EIO);
	input_levels(1, 1, 0);
	zassert_equal(parking_gpio_io.signal(PARKING_EXIT, true), -EIO);
	zassert_equal(write_pin_fake.call_count, 0);

	input_levels(1, 0, 0);
	write_pin_fake.return_val = -EIO;
	zassert_equal(parking_gpio_io.signal(PARKING_EXIT, true), -EIO);
	zassert_equal(write_pin_fake.call_count, 1);
	expect_write(0, &lane->red, 0);

	input_levels(1, 0, 0);
	write_pin_fake.return_val = 0;
	zassert_ok(parking_gpio_io.signal(PARKING_EXIT, true));
	zassert_equal(write_pin_fake.call_count, 3);
	expect_write(1, &lane->red, 0);
	expect_write(2, &lane->green, 1);
}

ZTEST(parking_gpio, test_red_signal_attempts_both_writes_on_error)
{
	const struct lane_gpio *lane = &pins[PARKING_ENTRY];
	int responses[] = {-EIO, -EPERM, 0, -EPERM};

	parking_gpio_init(&changed);
	SET_RETURN_SEQ(write_pin, responses, ARRAY_SIZE(responses));
	zassert_equal(parking_gpio_io.signal(PARKING_ENTRY, false), -EIO);
	zassert_equal(parking_gpio_io.signal(PARKING_ENTRY, false), -EPERM);
	zassert_equal(write_pin_fake.call_count, 4);
	expect_write(0, &lane->green, 0);
	expect_write(1, &lane->red, 1);
	expect_write(2, &lane->green, 0);
	expect_write(3, &lane->red, 1);
}

ZTEST(parking_gpio, test_buzzer_levels_and_unavailable_output)
{
	const struct lane_gpio *lane = &pins[PARKING_EXIT];

	parking_gpio_init(&changed);
	zassert_ok(parking_gpio_io.buzzer(PARKING_EXIT, true));
	zassert_ok(parking_gpio_io.buzzer(PARKING_EXIT, false));
	expect_write(0, &lane->buzzer, 1);
	expect_write(1, &lane->buzzer, 0);
	gpio_ready_fake.return_val = false;
	zassert_equal(parking_gpio_io.buzzer(PARKING_EXIT, true), -ENODEV);
	zassert_equal(parking_gpio_io.motor(PARKING_EXIT, PARKING_STOP), -ENODEV);
	zassert_equal(write_pin_fake.call_count, 2);
}

ZTEST_SUITE(parking_gpio, NULL, NULL, before_each, NULL, NULL);
