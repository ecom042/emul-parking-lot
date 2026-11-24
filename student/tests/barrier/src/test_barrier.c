/*
 * Testes da cancela (barrier.c): partida, abrir, fechar, prazos, obstrução,
 * fins de curso contraditórios e erros de driver.
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "barrier.h"
#include "config.h"
#include "hw_fake.h"

#define L LANE_ENTRY

static struct barrier b;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	hw_fake_reset();
	barrier_init(&b, L);
}

ZTEST_SUITE(barrier, NULL, NULL, before, NULL, NULL);

/* Posição da primeira escrita de 'out' com 'on' no histórico do falso. */
static int first_write(enum hw_out out, bool on)
{
	for (unsigned int i = 0; i < hw_out_set_fake.call_count && i < FFF_ARG_HISTORY_LEN; i++) {
		if (hw_out_set_fake.arg1_history[i] == out && hw_out_set_fake.arg2_history[i] == on) {
			return (int)i;
		}
	}
	return -1;
}

/* Abre a partir de fechada e chega ao fim de curso superior. */
static void open_fully(int64_t t)
{
	zassert_ok(barrier_open(&b, t));
	fake_in[L][HW_IN_LOWER] = 0;
	fake_in[L][HW_IN_UPPER] = 1;
	barrier_safety(&b);
	zassert_equal(barrier_step(&b, t + 2000), BARRIER_OPEN);
}

ZTEST(barrier, test_start_closed)
{
	zassert_equal(barrier_start(&b), BARRIER_CLOSED);
	zassert_equal(b.fault, BARRIER_FAULT_NONE);
}

ZTEST(barrier, test_start_without_lower_is_fault_not_homing)
{
	fake_in[L][HW_IN_LOWER] = 0;
	zassert_equal(barrier_start(&b), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_NOT_CLOSED);
	zassert_equal(fake_out_on_count[L][HW_OUT_ENABLE], 0, "nunca liga o motor");
}

ZTEST(barrier, test_start_contradictory)
{
	fake_in[L][HW_IN_UPPER] = 1;
	zassert_equal(barrier_start(&b), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_CONTRADICTORY);
}

ZTEST(barrier, test_start_read_error)
{
	fake_in[L][HW_IN_UPPER] = -EIO;
	zassert_equal(barrier_start(&b), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_IO);
}

ZTEST(barrier, test_start_lower_read_error)
{
	fake_in[L][HW_IN_LOWER] = -EIO;
	zassert_equal(barrier_start(&b), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_IO);
}

ZTEST(barrier, test_open_sets_direction_before_enable)
{
	zassert_ok(barrier_open(&b, 1000));
	zassert_equal(b.state, BARRIER_OPENING);
	zassert_true(fake_out[L][HW_OUT_DIRECTION]);
	zassert_true(fake_out[L][HW_OUT_ENABLE]);
	zassert_true(first_write(HW_OUT_DIRECTION, true) < first_write(HW_OUT_ENABLE, true));
	zassert_equal(hw_sched_lock_fake.call_count, 1);
	zassert_equal(hw_sched_unlock_fake.call_count, 1);
}

ZTEST(barrier, test_open_stops_at_upper_limit)
{
	zassert_ok(barrier_open(&b, 0));
	fake_in[L][HW_IN_LOWER] = 0;
	zassert_equal(barrier_step(&b, 1000), BARRIER_OPENING);
	zassert_true(fake_out[L][HW_OUT_ENABLE], "ainda subindo");

	/* A EXTI do superior chega: a work desliga o motor na hora. */
	fake_in[L][HW_IN_UPPER] = 1;
	barrier_safety(&b);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	zassert_equal(barrier_step(&b, 2000), BARRIER_OPEN);
}

ZTEST(barrier, test_open_detected_by_polling_too)
{
	zassert_ok(barrier_open(&b, 0));
	fake_in[L][HW_IN_LOWER] = 0;
	fake_in[L][HW_IN_UPPER] = 1;
	/* Sem a work: o próprio passo do laço confere e para. */
	zassert_equal(barrier_step(&b, 2000), BARRIER_OPEN);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
}

ZTEST(barrier, test_open_deadline_is_fault)
{
	zassert_ok(barrier_open(&b, 100));
	fake_in[L][HW_IN_LOWER] = 0; /* travado no meio: nenhum fim de curso */
	zassert_equal(barrier_step(&b, 100 + MOTOR_DEADLINE_MS - 1), BARRIER_OPENING);
	zassert_equal(barrier_step(&b, 100 + MOTOR_DEADLINE_MS), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_DEADLINE);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
}

ZTEST(barrier, test_open_only_from_closed)
{
	open_fully(0);
	zassert_equal(barrier_open(&b, 3000), -EPERM);
}

ZTEST(barrier, test_open_direction_error)
{
	fake_out_err[L][HW_OUT_DIRECTION] = -EIO;
	zassert_equal(barrier_open(&b, 0), -EIO);
	zassert_equal(b.state, BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_IO);
	zassert_equal(fake_out_on_count[L][HW_OUT_ENABLE], 0);
}

ZTEST(barrier, test_open_enable_error)
{
	fake_out_err[L][HW_OUT_ENABLE] = -EIO;
	zassert_equal(barrier_open(&b, 0), -EIO);
	zassert_equal(b.fault, BARRIER_FAULT_IO);
}

ZTEST(barrier, test_close_only_from_open)
{
	zassert_equal(barrier_close(&b, 0), -EPERM);
}

ZTEST(barrier, test_close_refused_with_beam_active)
{
	open_fully(0);
	fake_in[L][HW_IN_BEAM] = 1;
	zassert_equal(barrier_close(&b, 3000), -EPERM);
	zassert_equal(b.state, BARRIER_OPEN);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
}

ZTEST(barrier, test_close_beam_read_error)
{
	open_fully(0);
	fake_in[L][HW_IN_BEAM] = -EIO;
	zassert_equal(barrier_close(&b, 3000), -EIO);
}

ZTEST(barrier, test_close_stops_at_lower_limit)
{
	open_fully(0);
	zassert_ok(barrier_close(&b, 3000));
	zassert_false(fake_out[L][HW_OUT_DIRECTION], "direção de fechar");
	zassert_true(fake_out[L][HW_OUT_ENABLE]);
	fake_in[L][HW_IN_UPPER] = 0;
	fake_in[L][HW_IN_LOWER] = 1;
	barrier_safety(&b);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	zassert_equal(barrier_step(&b, 5000), BARRIER_CLOSED);
}

ZTEST(barrier, test_close_deadline_is_fault)
{
	open_fully(0);
	zassert_ok(barrier_close(&b, 3000));
	fake_in[L][HW_IN_UPPER] = 0; /* inferior nunca acende */
	zassert_equal(barrier_step(&b, 3000 + MOTOR_DEADLINE_MS), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_DEADLINE);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
}

ZTEST(barrier, test_obstruction_while_closing_stops_without_reversing)
{
	int enables;

	open_fully(0);
	zassert_ok(barrier_close(&b, 3000));
	fake_in[L][HW_IN_UPPER] = 0;
	enables = fake_out_on_count[L][HW_OUT_ENABLE];

	fake_in[L][HW_IN_BEAM] = 1;
	barrier_safety(&b); /* work da EXTI do feixe */
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	zassert_equal(barrier_step(&b, 3500), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_OBSTRUCTION);

	/* O feixe libera: continua parado, sem religar nem reverter. */
	fake_in[L][HW_IN_BEAM] = 0;
	barrier_safety(&b);
	zassert_equal(barrier_step(&b, 4000), BARRIER_FAULT);
	zassert_equal(fake_out_on_count[L][HW_OUT_ENABLE], enables);
	zassert_false(fake_out[L][HW_OUT_DIRECTION]);
}

ZTEST(barrier, test_contradictory_while_moving)
{
	zassert_ok(barrier_open(&b, 0));
	fake_in[L][HW_IN_UPPER] = 1; /* inferior ainda 1 */
	barrier_safety(&b);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	zassert_equal(barrier_step(&b, 100), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_CONTRADICTORY);
}

ZTEST(barrier, test_contradictory_while_closed)
{
	zassert_equal(barrier_start(&b), BARRIER_CLOSED);
	fake_in[L][HW_IN_UPPER] = 1;
	zassert_equal(barrier_step(&b, 100), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_CONTRADICTORY);
}

ZTEST(barrier, test_lower_lost_while_closed)
{
	zassert_equal(barrier_start(&b), BARRIER_CLOSED);
	fake_in[L][HW_IN_LOWER] = 0;
	zassert_equal(barrier_step(&b, 100), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_NOT_CLOSED);
}

ZTEST(barrier, test_open_state_is_kept)
{
	open_fully(0);
	zassert_equal(barrier_step(&b, 4000), BARRIER_OPEN);
}

ZTEST(barrier, test_read_error_stops_motor)
{
	zassert_ok(barrier_open(&b, 0));
	fake_in[L][HW_IN_BEAM] = -EIO;
	barrier_safety(&b);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	zassert_equal(barrier_step(&b, 100), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_IO);
}

ZTEST(barrier, test_motor_off_error_is_reported)
{
	zassert_ok(barrier_open(&b, 0));
	fake_in[L][HW_IN_LOWER] = 0;
	fake_in[L][HW_IN_UPPER] = 1;
	fake_out_err[L][HW_OUT_ENABLE] = -EIO;
	zassert_equal(barrier_step(&b, 2000), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_IO);
}

ZTEST(barrier, test_fault_is_sticky)
{
	barrier_fail(&b, BARRIER_FAULT_DEADLINE);
	zassert_equal(barrier_step(&b, 100), BARRIER_FAULT);
	zassert_equal(barrier_open(&b, 100), -EPERM);
	zassert_equal(b.fault, BARRIER_FAULT_DEADLINE);
}

ZTEST(barrier, test_start_fully_open_is_not_closed)
{
	fake_in[L][HW_IN_LOWER] = 0;
	fake_in[L][HW_IN_UPPER] = 1;
	zassert_equal(barrier_start(&b), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_NOT_CLOSED);
}

ZTEST(barrier, test_upper_or_lower_read_error_stops_motor)
{
	zassert_ok(barrier_open(&b, 0));
	fake_in[L][HW_IN_UPPER] = -EIO;
	barrier_safety(&b);
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	zassert_equal(barrier_step(&b, 10), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_IO);

	barrier_init(&b, L);
	fake_in[L][HW_IN_UPPER] = 0;
	zassert_ok(barrier_open(&b, 0));
	fake_in[L][HW_IN_LOWER] = -EIO;
	barrier_safety(&b);
	zassert_equal(barrier_step(&b, 10), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_IO);
}

/* O relógio da work (hw_now_ms) passou do prazo: ela mesma para o motor. */
static int64_t fake_clock;

static int64_t clock_custom(void)
{
	return fake_clock;
}

ZTEST(barrier, test_deadline_enforced_by_work_without_the_loop)
{
	hw_now_ms_fake.custom_fake = clock_custom;
	fake_clock = 100;
	zassert_ok(barrier_open(&b, 100));
	zassert_equal(hw_wake_after_fake.call_count, 1, "work agendada para o prazo");
	zassert_equal(hw_wake_after_fake.arg1_val, MOTOR_DEADLINE_MS);
	fake_in[L][HW_IN_LOWER] = 0;

	fake_clock = 100 + MOTOR_DEADLINE_MS - 1;
	barrier_safety(&b);
	zassert_true(fake_out[L][HW_OUT_ENABLE]);
	fake_clock = 100 + MOTOR_DEADLINE_MS;
	barrier_safety(&b); /* a work agendada roda no instante do prazo */
	zassert_false(fake_out[L][HW_OUT_ENABLE]);
	/* O laço, mesmo atrasado, só registra a falha. */
	zassert_equal(barrier_step(&b, 100 + MOTOR_DEADLINE_MS - 50), BARRIER_FAULT);
	zassert_equal(b.fault, BARRIER_FAULT_DEADLINE);
}
