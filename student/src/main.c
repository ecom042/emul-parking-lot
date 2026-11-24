/*
 * Firmware do estacionamento: laço de controle e thread do display.
 *
 * Threads:
 *  - main (prioridade alta, o laço de controle): a cada volta, atende as
 *    duas faixas (lane_tick), as três UARTs, o ADC e o display. Nunca
 *    bloqueia: só dorme até o próximo evento (EXTI, byte de UART, display
 *    pronto) ou no máximo LOOP_PERIOD_MS;
 *  - display (prioridade baixa): desenha no ST7789V quando o laço pede. O
 *    SPI bloqueia enquanto envia, mas o laço tem prioridade maior e
 *    interrompe a thread do display sempre que precisa (REQ-04: a espera
 *    de uma faixa não atrasa a segurança da outra).
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "adc_sampler.h"
#include "display.h"
#include "hw.h"
#include "lane.h"
#include "line_parser.h"
#include "sessions.h"
#include "ui.h"

/** Período máximo do laço. Os prazos têm folga de 10 ms; 2 ms basta. */
#define LOOP_PERIOD_MS 2

#define DISPLAY_STACK 1536
#define DISPLAY_PRIORITY 10

static struct lane lanes[LANE_COUNT];
static struct line_parser parsers[UART_COUNT];

/* ---- Thread do display -------------------------------------------------- */

struct ui_request {
	bool amount_step; /* false: placa; true: valor e avisos */
	char plate[PLATE_LEN + 1];
	uint32_t centavos;
};

K_MSGQ_DEFINE(ui_queue, sizeof(struct ui_request), 2, 4);

/** Resultado da última etapa: 0 nada, 1 ok, 2 erro (lido pelo laço). */
static atomic_t ui_done;

int ui_request_plate(const char *plate)
{
	struct ui_request req = {.amount_step = false};

	memcpy(req.plate, plate, sizeof(req.plate));
	return k_msgq_put(&ui_queue, &req, K_NO_WAIT);
}

int ui_request_amount(uint32_t centavos)
{
	struct ui_request req = {.amount_step = true, .centavos = centavos};

	return k_msgq_put(&ui_queue, &req, K_NO_WAIT);
}

static void display_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	struct ui_request req;
	/* A inicialização (cerca de 400 ms de esperas) acontece em paralelo. */
	int init_rc = display_init();

	printk("display init %d\n", init_rc);
	for (;;) {
		(void)k_msgq_get(&ui_queue, &req, K_FOREVER);
		int rc = init_rc;

		if (rc == 0) { /* LCOV_EXCL_LINE: a inicialização do display não falha no modelo */
			rc = req.amount_step ? display_show_amount(req.centavos)
					     : display_show_plate(req.plate);
		}
		atomic_set(&ui_done, rc == 0 ? 1 : 2); /* LCOV_EXCL_LINE: o SPI não falha no modelo; o caso de erro é testado em tests/faixa */
		hw_wake();
	}
}

K_THREAD_DEFINE(display_tid, DISPLAY_STACK, display_thread, NULL, NULL, NULL,
		DISPLAY_PRIORITY, 0, 0);

/* ---- Laço de controle --------------------------------------------------- */

/** Lê os bytes de uma UART e entrega as linhas completas à faixa certa. */
static void service_uart(enum uart_ch ch, int64_t now)
{
	uint8_t bytes[32];
	size_t n;

	while ((n = hw_uart_read(ch, bytes, sizeof(bytes))) > 0U) {
		for (size_t i = 0; i < n; i++) {
			if (line_parser_feed(&parsers[ch], bytes[i], now) < 0) {
				continue;
			}
			const char *line = parsers[ch].buf;

			if (ch == UART_PAYMENT) {
				lane_on_payment_line(&lanes[LANE_EXIT], line, now);
			} else {
				lane_on_camera_line(&lanes[ch == UART_CAM_ENTRY ? LANE_ENTRY : LANE_EXIT],
						    line, now);
			}
		}
	}
	line_parser_poll(&parsers[ch], now);
}

int main(void)
{
	int64_t now = hw_now_ms();
	bool adc_fault_reported = false;

	if (hw_init() != 0) { /* LCOV_EXCL_LINE: os drivers sempre ficam prontos no Renode */
		/* Nada é seguro sem os drivers: fica parado com o vermelho aceso. */
		printk("PARKING: hardware init failed\n"); /* LCOV_EXCL_LINE: drivers sempre prontos no Renode */
		return 0;                                 /* LCOV_EXCL_LINE: idem */
	}
	sessions_reset();
	adc_sampler_reset();
	lane_module_reset();
	line_parser_init(&parsers[UART_CAM_ENTRY], LINE_CRLF);
	line_parser_init(&parsers[UART_CAM_EXIT], LINE_CRLF);
	line_parser_init(&parsers[UART_PAYMENT], LINE_LF);
	lane_init(&lanes[LANE_ENTRY], LANE_ENTRY, now);
	lane_init(&lanes[LANE_EXIT], LANE_EXIT, now);
	printk("PARKING: ready (capacity %d, tariff unit %d ms)\n", CONFIG_PARKING_CAPACITY,
	       CONFIG_PARKING_TARIFF_UNIT_MS);

	for (;;) {
		struct adc_sample sample;

		now = hw_now_ms();

		/* 1. Segurança primeiro: fins de curso, feixe, prazos, buzzer. */
		lane_tick(&lanes[LANE_ENTRY], now);
		lane_tick(&lanes[LANE_EXIT], now);

		/* 2. Linhas completas das três UARTs. */
		for (int ch = 0; ch < UART_COUNT; ch++) {
			service_uart((enum uart_ch)ch, now);
		}

		/* 3. ADC4: no máximo uma amostra nova por volta. */
		if (adc_sampler_poll(now, &sample)) {
			lane_on_distance(&lanes[sample.lane], sample.code, now);
		}
		if (adc_sampler_failed() && !adc_fault_reported) { /* LCOV_EXCL_LINE: o modelo não produz falha de ADC; testada em tests/logica e tests/faixa */
			adc_fault_reported = true; /* LCOV_EXCL_LINE: o modelo não produz falha de ADC */
			lane_fault(&lanes[LANE_ENTRY], now); /* LCOV_EXCL_LINE: idem */
			lane_fault(&lanes[LANE_EXIT], now);  /* LCOV_EXCL_LINE: idem */
		}

		/* 4. A thread do display terminou uma etapa? */
		atomic_val_t done = atomic_set(&ui_done, 0);

		if (done != 0) {
			lane_on_display_done(&lanes[LANE_EXIT], done == 1, now);
		}

		hw_wait_event(LOOP_PERIOD_MS);
	}
	return 0;
}
