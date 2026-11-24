/*
 * Cancela de uma faixa: motor (enable + direção) e fins de curso.
 *
 * Dois contextos usam este módulo:
 *  - o laço principal chama barrier_open/close/step (estados e prazos);
 *  - a work de entrada (disparada pela EXTI) chama barrier_safety, que só
 *    desliga o motor quando chega ao fim de curso, há obstrução ou os fins
 *    de curso se contradizem.
 * Os dois conversam apenas por variáveis atômicas (motion e flags).
 */
#ifndef PARKING_BARRIER_H
#define PARKING_BARRIER_H

#include <stdint.h>

#include <zephyr/sys/atomic.h>

#include "hw.h"

/** Estado visto pelo laço principal. */
enum barrier_state {
	BARRIER_CLOSED = 0, /* parada no fim de curso inferior */
	BARRIER_OPENING,
	BARRIER_OPEN,       /* parada no fim de curso superior, motor desligado */
	BARRIER_CLOSING,
	BARRIER_FAULT,      /* motor desligado; ver barrier.fault */
};

/** Motivo de uma falha da cancela. */
enum barrier_fault {
	BARRIER_FAULT_NONE = 0,
	BARRIER_FAULT_CONTRADICTORY, /* superior e inferior ao mesmo tempo */
	BARRIER_FAULT_DEADLINE,      /* movimento passou de MOTOR_DEADLINE_MS */
	BARRIER_FAULT_OBSTRUCTION,   /* feixe ativo durante o fechamento */
	BARRIER_FAULT_NOT_CLOSED,    /* em repouso sem o fim de curso inferior */
	BARRIER_FAULT_IO,            /* erro de driver ao ler ou escrever */
};

/* Movimento em curso, compartilhado com a work (atomic_t). */
#define BARRIER_MOTION_NONE 0
#define BARRIER_MOTION_OPENING 1
#define BARRIER_MOTION_CLOSING 2

/* Bits que a work liga em flags para o laço principal consumir. */
#define BARRIER_FLAG_ARRIVED BIT(0)       /* chegou ao fim de curso pedido */
#define BARRIER_FLAG_OBSTRUCTED BIT(1)    /* parou por obstrução */
#define BARRIER_FLAG_CONTRADICTORY BIT(2) /* fins de curso contraditórios */
#define BARRIER_FLAG_IO BIT(3)            /* erro de driver */
#define BARRIER_FLAG_DEADLINE BIT(4)      /* prazo do movimento estourou */

struct barrier {
	enum lane_id lane;
	enum barrier_state state;  /* só o laço principal escreve */
	enum barrier_fault fault;  /* válido em BARRIER_FAULT */
	int64_t deadline_ms;       /* fim do prazo do movimento atual */
	atomic_t motion;           /* BARRIER_MOTION_* */
	atomic_t flags;            /* BARRIER_FLAG_* */
};

/** Estado inicial: fechada, sem movimento. Não acessa o hardware. */
void barrier_init(struct barrier *b, enum lane_id lane);

/**
 * Confere os fins de curso na partida (REQ-01).
 * @return BARRIER_CLOSED ou BARRIER_FAULT (contraditórios, sem inferior, IO).
 */
enum barrier_state barrier_start(struct barrier *b);

/** Começa a abrir. Só a partir de BARRIER_CLOSED. @return 0, -EPERM ou -EIO. */
int barrier_open(struct barrier *b, int64_t now_ms);

/** Começa a fechar. Só a partir de BARRIER_OPEN. @return 0, -EPERM ou -EIO. */
int barrier_close(struct barrier *b, int64_t now_ms);

/** Desliga o motor e entra em falha com o motivo dado. */
void barrier_fail(struct barrier *b, enum barrier_fault fault);

/**
 * Parada urgente. Chamado pela work de entrada (EXTI ou prazo) e também a
 * cada tick. Lê as entradas e o relógio e desliga o motor se for o caso;
 * nunca liga nada.
 */
void barrier_safety(struct barrier *b);

/** Avança estados e prazos. Chamado a cada tick do laço principal. */
enum barrier_state barrier_step(struct barrier *b, int64_t now_ms);

#endif /* PARKING_BARRIER_H */
