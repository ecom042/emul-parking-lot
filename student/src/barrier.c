/*
 * Cancela: estados, prazos e parada pelo fim de curso real (REQ-01, 06, 08).
 */
#include "barrier.h"

#include <errno.h>
#include <stdbool.h>

#include "config.h"

void barrier_init(struct barrier *b, enum lane_id lane)
{
	b->lane = lane;
	b->state = BARRIER_CLOSED;
	b->fault = BARRIER_FAULT_NONE;
	b->deadline_ms = 0;
	atomic_set(&b->motion, BARRIER_MOTION_NONE);
	atomic_set(&b->flags, 0);
}

/* Desliga o motor. Em erro de driver marca IO para o laço tratar. */
static void motor_off(struct barrier *b)
{
	if (hw_out_set(b->lane, HW_OUT_ENABLE, false) != 0) {
		atomic_or(&b->flags, BARRIER_FLAG_IO);
	}
}

void barrier_fail(struct barrier *b, enum barrier_fault fault)
{
	atomic_set(&b->motion, BARRIER_MOTION_NONE);
	motor_off(b);
	b->state = BARRIER_FAULT;
	b->fault = fault;
}

enum barrier_state barrier_start(struct barrier *b)
{
	int upper = hw_in_get(b->lane, HW_IN_UPPER);
	int lower = hw_in_get(b->lane, HW_IN_LOWER);

	if (upper < 0 || lower < 0) {
		barrier_fail(b, BARRIER_FAULT_IO);
	} else if (upper == 1 && lower == 1) {
		barrier_fail(b, BARRIER_FAULT_CONTRADICTORY);
	} else if (lower == 0) {
		/* Braço fora de 0°: pede ajuda, nunca fecha sozinho. */
		barrier_fail(b, BARRIER_FAULT_NOT_CLOSED);
	} else {
		b->state = BARRIER_CLOSED;
	}
	return b->state;
}

/*
 * Liga o motor no sentido pedido. A direção é fixada antes do enable, e o
 * par "marcar movimento + ligar" roda sem troca de thread, para que a work
 * de parada nunca veja o motor ligado sem saber o movimento.
 */
static int motor_start(struct barrier *b, bool open, int64_t now_ms)
{
	int rc;

	hw_sched_lock();
	b->deadline_ms = now_ms + MOTOR_DEADLINE_MS;
	rc = hw_out_set(b->lane, HW_OUT_DIRECTION, open);
	if (rc == 0) {
		atomic_set(&b->flags, 0);
		atomic_set(&b->motion, open ? BARRIER_MOTION_OPENING : BARRIER_MOTION_CLOSING);
		rc = hw_out_set(b->lane, HW_OUT_ENABLE, true);
	}
	hw_sched_unlock();

	if (rc != 0) {
		barrier_fail(b, BARRIER_FAULT_IO);
		return -EIO;
	}
	b->state = open ? BARRIER_OPENING : BARRIER_CLOSING;
	/* O prazo é conferido também pela work agendada para o instante exato. */
	hw_wake_after(b->lane, MOTOR_DEADLINE_MS);
	/* O fim de curso pode já estar lá: confere na hora. */
	barrier_safety(b);
	return 0;
}

int barrier_open(struct barrier *b, int64_t now_ms)
{
	if (b->state != BARRIER_CLOSED) {
		return -EPERM;
	}
	return motor_start(b, true, now_ms);
}

int barrier_close(struct barrier *b, int64_t now_ms)
{
	int beam;

	if (b->state != BARRIER_OPEN) {
		return -EPERM;
	}
	/* REQ-08: nunca começar a fechar com algo no feixe. */
	beam = hw_in_get(b->lane, HW_IN_BEAM);
	if (beam != 0) {
		return beam < 0 ? -EIO : -EPERM;
	}
	return motor_start(b, false, now_ms);
}

void barrier_safety(struct barrier *b)
{
	int upper = hw_in_get(b->lane, HW_IN_UPPER);
	int lower = hw_in_get(b->lane, HW_IN_LOWER);
	int beam = hw_in_get(b->lane, HW_IN_BEAM);
	atomic_val_t motion = atomic_get(&b->motion);

	if (upper < 0 || lower < 0 || beam < 0) {
		motor_off(b);
		atomic_set(&b->motion, BARRIER_MOTION_NONE);
		atomic_or(&b->flags, BARRIER_FLAG_IO);
		return;
	}
	if (upper == 1 && lower == 1) {
		motor_off(b);
		atomic_set(&b->motion, BARRIER_MOTION_NONE);
		atomic_or(&b->flags, BARRIER_FLAG_CONTRADICTORY);
		return;
	}
	if (motion == BARRIER_MOTION_OPENING && upper == 1) {
		motor_off(b);
		atomic_set(&b->motion, BARRIER_MOTION_NONE);
		atomic_or(&b->flags, BARRIER_FLAG_ARRIVED);
	} else if (motion == BARRIER_MOTION_CLOSING && beam == 1) {
		/* REQ-08: obstrução no fechamento; para e não reverte. */
		motor_off(b);
		atomic_set(&b->motion, BARRIER_MOTION_NONE);
		atomic_or(&b->flags, BARRIER_FLAG_OBSTRUCTED);
	} else if (motion == BARRIER_MOTION_CLOSING && lower == 1) {
		motor_off(b);
		atomic_set(&b->motion, BARRIER_MOTION_NONE);
		atomic_or(&b->flags, BARRIER_FLAG_ARRIVED);
	} else if (motion != BARRIER_MOTION_NONE && hw_now_ms() >= b->deadline_ms) {
		/* REQ-06: o modelo nunca desliga o motor; o prazo desliga. */
		motor_off(b);
		atomic_set(&b->motion, BARRIER_MOTION_NONE);
		atomic_or(&b->flags, BARRIER_FLAG_DEADLINE);
	}
}

enum barrier_state barrier_step(struct barrier *b, int64_t now_ms)
{
	atomic_val_t flags;

	/* Segunda linha de defesa: a mesma checagem da work, por polling. */
	barrier_safety(b);
	flags = atomic_set(&b->flags, 0);

	if (b->state == BARRIER_FAULT) {
		return b->state;
	}
	if ((flags & BARRIER_FLAG_IO) != 0) {
		barrier_fail(b, BARRIER_FAULT_IO);
	} else if ((flags & BARRIER_FLAG_CONTRADICTORY) != 0) {
		barrier_fail(b, BARRIER_FAULT_CONTRADICTORY);
	} else if (b->state == BARRIER_OPENING || b->state == BARRIER_CLOSING) {
		bool opening = b->state == BARRIER_OPENING;

		if ((flags & BARRIER_FLAG_OBSTRUCTED) != 0) {
			barrier_fail(b, BARRIER_FAULT_OBSTRUCTION);
		} else if ((flags & BARRIER_FLAG_ARRIVED) != 0) {
			b->state = opening ? BARRIER_OPEN : BARRIER_CLOSED;
		} else if ((flags & BARRIER_FLAG_DEADLINE) != 0 || now_ms >= b->deadline_ms) {
			barrier_fail(b, BARRIER_FAULT_DEADLINE);
		}
	} else if (b->state == BARRIER_CLOSED && hw_in_get(b->lane, HW_IN_LOWER) != 1) {
		/* Em repouso, o fim de curso inferior sumiu. */
		barrier_fail(b, BARRIER_FAULT_NOT_CLOSED);
	}
	return b->state;
}
