/*
 * payment.c — máquina de estados da cobrança. Contrato e visão geral: payment.h.
 *
 *            payment_start()                  (só registra o pedido)
 *                  │
 *             [STARTING] ─ esvazia a UART (lixo de antes), gera UUID, pede a placa ao display
 *                  ▼
 *              [PLATE]   ─ display terminou? ──► CONGELA a tarifa ─► pede a cobrança ao display
 *                  ▼
 *              [SCREEN]  ─ display terminou? ──► envia "P1 CHARGE" (envio 1/3)
 *                  ▼
 *              [WAITING] ─ lê o terminal:
 *                  │        PAID  com UUID atual e valor congelado ─► OK
 *                  │        CANCELLED com UUID atual e valor igual ─► FAILED
 *                  │        1000 ms sem resposta ─► reenvia igual (até 3 envios)
 *                  │        3 envios esgotados ─► invalida, "P1 CANCEL", FAILED
 *                  ▼
 *               [IDLE]
 *
 * Qualquer falha de display/UART/tarifa leva a FAILED (e a [IDLE]).
 */
#include <errno.h>
#include <string.h>

#include "payment.h"
#include "payment_hw.h"
#include "payment_parser.h"
#include "payment_uuid.h"
#include "tariff.h"

/* A unidade de tarifa vem do Kconfig do firmware (60000 ms padrão; 1000 ms nos cenários). */
#define TARIFF_UNIT_MS ((uint32_t)CONFIG_PARKING_TARIFF_UNIT_MS)

/* "P1 CHARGE " (10) + uuid (36) + " " + placa (7) + " " + valor (até 7) + " BRL\n" (5) = 67 */
#define LINE_MAX 80U

_Static_assert(PARKING_PLATE_SIZE == 8U, "placa: 7 caracteres + NUL");

enum payment_phase {
	PAY_IDLE,     /* nada em andamento */
	PAY_STARTING, /* pedido registrado; falta começar no próximo poll */
	PAY_PLATE,    /* display desenhando a placa */
	PAY_SCREEN,   /* display desenhando a cobrança */
	PAY_WAITING,  /* cobrança enviada; esperando o terminal */
};

enum payment_verdict {
	VERDICT_NONE,
	VERDICT_PAID,
	VERDICT_CANCELLED,
};

static struct {
	enum payment_phase phase;
	char plate[PARKING_PLATE_SIZE];
	int64_t entered_at;
	char uuid[PAYMENT_UUID_SIZE]; /* UUID da tentativa atual (vazio = nenhuma) */
	uint32_t amount;              /* valor CONGELADO, em centavos */
	uint8_t sends;                /* envios de CHARGE já feitos */
	int64_t deadline;             /* quando o envio atual vence */
	uint64_t counter;             /* próximo contador de UUID (só cresce) */
	struct payment_parser parser;
} pay;

static const struct payment_display_ops *screen;

/* ---- montagem das linhas P1 (sem snprintf: simples e previsível) ---- */

static size_t append_text(char *out, size_t pos, const char *text)
{
	size_t length = strlen(text);

	memcpy(&out[pos], text, length);
	return pos + length;
}

static size_t append_number(char *out, size_t pos, uint32_t value)
{
	char digits[10];
	size_t count = 0U;

	do {
		digits[count++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value > 0U);
	while (count > 0U) {
		out[pos++] = digits[--count];
	}
	return pos;
}

/* "P1 CHARGE <uuid> <placa> <valor> BRL\n" — o MESMO texto em toda retransmissão. */
static size_t build_charge(char out[LINE_MAX])
{
	size_t pos = append_text(out, 0U, "P1 CHARGE ");

	pos = append_text(out, pos, pay.uuid);
	pos = append_text(out, pos, " ");
	pos = append_text(out, pos, pay.plate);
	pos = append_text(out, pos, " ");
	pos = append_number(out, pos, pay.amount);
	return append_text(out, pos, " BRL\n");
}

static size_t build_cancel(char out[LINE_MAX])
{
	size_t pos = append_text(out, 0U, "P1 CANCEL ");

	pos = append_text(out, pos, pay.uuid);
	return append_text(out, pos, "\n");
}

/* ---- transições ---- */

/* Termina a cobrança: invalida o UUID e volta a IDLE. */
static void finish(void)
{
	pay.phase = PAY_IDLE;
	memset(pay.uuid, 0, sizeof(pay.uuid));
	pay.amount = 0U;
}

static enum equipment_result fail(void)
{
	finish();
	return EQUIPMENT_FAILED;
}

/*
 * Desiste de uma cobrança JÁ ENVIADA. Ordem importa: monta o CANCEL, INVALIDA o
 * UUID (a partir daqui nenhuma resposta dele vale) e só então envia. O envio é
 * "melhor esforço": se falhar, a cobrança já está invalidada do nosso lado.
 */
static void abort_charge(void)
{
	char line[LINE_MAX];
	size_t length = build_cancel(line);

	finish();
	(void)payment_hw_send((const uint8_t *)line, length);
}

/* Envia a cobrança (ou a retransmissão) e arma o prazo de 1000 ms. */
static int send_charge(int64_t now)
{
	char line[LINE_MAX];
	size_t length = build_charge(line);
	int rc = payment_hw_send((const uint8_t *)line, length);

	if (rc < 0) {
		return rc;
	}
	pay.sends++;
	pay.deadline = now + PAYMENT_ATTEMPT_TIMEOUT_MS;
	return 0;
}

/*
 * Este quadro decide a cobrança? Só um RESULT com o UUID ATUAL e o valor
 * CONGELADO. Outro UUID (resposta velha), outro valor, ERROR e INVALID nunca
 * decidem.
 */
static enum payment_verdict judge(const struct payment_frame *frame)
{
	if (frame->kind != PAYMENT_FRAME_RESULT || strcmp(frame->uuid, pay.uuid) != 0 ||
	    frame->amount != pay.amount) {
		return VERDICT_NONE;
	}
	return frame->status == PAYMENT_STATUS_PAID ? VERDICT_PAID : VERDICT_CANCELLED;
}

/*
 * Lê o que já chegou do terminal (limitado a PAYMENT_READ_ROUNDS blocos, para um
 * fluxo sem fim não prender o firmware), monta as linhas e julga. O primeiro
 * quadro decisivo vence. Devolve < 0 se a leitura falhou.
 */
static int pump(int64_t now, enum payment_verdict *verdict)
{
	uint8_t chunk[PAYMENT_READ_CHUNK];

	*verdict = VERDICT_NONE;
	for (int round = 0; round < PAYMENT_READ_ROUNDS; ++round) {
		int count = payment_hw_read(chunk, sizeof(chunk));

		if (count < 0) {
			return count;
		}
		for (int i = 0; i < count; ++i) {
			struct payment_frame frame;

			payment_parser_feed(&pay.parser, chunk[i], now, &frame);
			if (*verdict == VERDICT_NONE) {
				*verdict = judge(&frame);
			}
		}
		if ((size_t)count < sizeof(chunk)) {
			break;
		}
	}
	return 0;
}

/* [STARTING]: limpa a UART, gera o UUID e pede a placa ao display. */
static enum equipment_result begin(int64_t now)
{
	enum payment_verdict ignored;

	/* Bytes que sobraram (ex.: um PAID repetido de uma saída anterior) são lixo. */
	if (pump(now, &ignored) < 0) {
		return fail();
	}
	payment_uuid_make(pay.counter++, pay.uuid);
	if (screen->draw_plate(pay.plate) < 0) {
		return fail();
	}
	pay.phase = PAY_PLATE;
	return EQUIPMENT_PENDING;
}

/* [PLATE]: placa desenhada -> congela a tarifa e pede a tela de cobrança. */
static enum equipment_result plate_drawn(int64_t now)
{
	enum equipment_result drawing = screen->poll(now);

	if (drawing == EQUIPMENT_PENDING) {
		return EQUIPMENT_PENDING;
	}
	if (drawing == EQUIPMENT_FAILED) {
		return fail();
	}
	/* AQUI o valor é congelado: depois deste ponto ele não é mais calculado. */
	if (!tariff_compute(pay.entered_at, now, TARIFF_UNIT_MS, &pay.amount)) {
		return fail(); /* acima de 9999 unidades: assistência */
	}
	if (screen->draw_charge(pay.plate, pay.amount) < 0) {
		return fail();
	}
	pay.phase = PAY_SCREEN;
	return EQUIPMENT_PENDING;
}

/* [SCREEN]: cobrança desenhada -> envia P1 CHARGE (envio 1). */
static enum equipment_result screen_drawn(int64_t now)
{
	enum equipment_result drawing = screen->poll(now);

	if (drawing == EQUIPMENT_PENDING) {
		return EQUIPMENT_PENDING;
	}
	if (drawing == EQUIPMENT_FAILED) {
		return fail();
	}
	if (send_charge(now) < 0) {
		return fail();
	}
	pay.phase = PAY_WAITING;
	return EQUIPMENT_PENDING;
}

/*
 * [WAITING]: primeiro lê o terminal, depois olha o prazo.
 *
 * Por que aceitar um PAID exato mesmo no instante em que o prazo vence? Porque
 * depois do prazo final enviaríamos P1 CANCEL, e um PAID que já começou a ser
 * transmitido NÃO pode ser desfeito: o cliente pagaria e a cancela não abriria.
 * Aceitar só vale com UUID atual e valor exato, então é seguro.
 */
static enum equipment_result await_result(int64_t now)
{
	enum payment_verdict verdict;

	if (pump(now, &verdict) < 0) {
		abort_charge();
		return EQUIPMENT_FAILED;
	}
	if (verdict == VERDICT_PAID) {
		finish();
		return EQUIPMENT_OK;
	}
	if (verdict == VERDICT_CANCELLED) {
		return fail(); /* o terminal cancelou: nada a desfazer, só assistência */
	}
	if (now < pay.deadline) {
		return EQUIPMENT_PENDING;
	}
	/* 1000 ms sem resposta: reenvia igual, ou desiste depois do 3º envio. */
	if (pay.sends >= PAYMENT_ATTEMPTS || send_charge(now) < 0) {
		abort_charge();
		return EQUIPMENT_FAILED;
	}
	return EQUIPMENT_PENDING;
}

/* ---- API pública ---- */

void payment_reset(void)
{
	uint64_t counter = pay.counter; /* o contador de UUID nunca volta atrás */

	memset(&pay, 0, sizeof(pay));
	pay.counter = counter;
	payment_parser_init(&pay.parser);
}

int payment_init(const struct payment_display_ops *display)
{
	int rc;

	/* Qualquer falha abaixo deixa o módulo "não inicializado": payment_start() => -ENODEV. */
	screen = NULL;
	if (display == NULL || display->draw_plate == NULL || display->draw_charge == NULL ||
	    display->poll == NULL || display->cancel == NULL) {
		return -EINVAL;
	}
	payment_reset();
	rc = payment_hw_init();
	if (rc < 0) {
		return rc;
	}
	screen = display;
	return 0;
}

int payment_start(const char plate[PARKING_PLATE_SIZE], int64_t entered_at)
{
	if (screen == NULL) {
		return -ENODEV;
	}
	if (pay.phase != PAY_IDLE) {
		return -EBUSY;
	}
	if (plate == NULL || !payment_plate_valid(plate)) {
		return -EINVAL;
	}
	memcpy(pay.plate, plate, PARKING_PLATE_SIZE);
	pay.entered_at = entered_at;
	pay.sends = 0U;
	pay.phase = PAY_STARTING;
	return 0;
}

enum equipment_result payment_poll(int64_t now)
{
	if (pay.phase == PAY_STARTING) {
		return begin(now);
	}
	if (pay.phase == PAY_PLATE) {
		return plate_drawn(now);
	}
	if (pay.phase == PAY_SCREEN) {
		return screen_drawn(now);
	}
	if (pay.phase == PAY_WAITING) {
		return await_result(now);
	}
	return EQUIPMENT_FAILED; /* sem cobrança em andamento: nunca autoriza */
}

void payment_cancel(void)
{
	if (pay.phase == PAY_WAITING) {
		abort_charge();
	} else if (pay.phase == PAY_PLATE || pay.phase == PAY_SCREEN) {
		screen->cancel();
	}
	finish();
}
