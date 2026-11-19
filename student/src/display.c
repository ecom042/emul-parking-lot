/*
 * display.c — máquina de estados do display. Visão geral: veja display.h.
 *
 *   INICIALIZAÇÃO (init)                       DESENHO (job)
 *   OFF ─display_init()─► RUNNING ─► READY     NONE ─draw_*()─► PENDING ─► DONE
 *                            │                                      │
 *                            └─ erro ─► FAILED                      └─ erro ─► FAILED
 *
 * Um desenho pedido ANTES da inicialização terminar simplesmente espera: ele
 * fica PENDING e começa a pintar no instante em que a inicialização chega a READY.
 */
#include <errno.h>
#include <string.h>

#include "display.h"
#include "display_hw.h"
#include "display_render.h"

/* Comandos do ST7789V usados. */
#define CMD_SWRESET 0x01U
#define CMD_SLPOUT 0x11U
#define CMD_DISPON 0x29U
#define CMD_CASET 0x2AU  /* janela: colunas */
#define CMD_RASET 0x2BU  /* janela: linhas */
#define CMD_RAMWR 0x2CU  /* a seguir vêm os pixels */
#define CMD_MADCTL 0x36U
#define CMD_COLMOD 0x3AU

enum step_kind {
	STEP_RESET_ASSERT,  /* baixa a linha de reset */
	STEP_RESET_RELEASE, /* sobe a linha de reset */
	STEP_COMMAND,       /* envia um comando (com 0 ou 1 parâmetro) */
};

struct init_step {
	enum step_kind kind;
	uint8_t command;
	uint8_t param;
	bool has_param;
	uint16_t wait_ms; /* quanto esperar DEPOIS deste passo */
};

/* A sequência do contrato, com as esperas. Soma das esperas: 10+120+150+120 = 400 ms. */
static const struct init_step init_steps[] = {
	{ STEP_RESET_ASSERT, 0U, 0U, false, 10U },
	{ STEP_RESET_RELEASE, 0U, 0U, false, 120U },
	{ STEP_COMMAND, CMD_SWRESET, 0U, false, 150U },
	{ STEP_COMMAND, CMD_SLPOUT, 0U, false, 120U },
	{ STEP_COMMAND, CMD_COLMOD, 0x55U, true, 0U }, /* 16 bits por pixel: RGB565 */
	{ STEP_COMMAND, CMD_MADCTL, 0x00U, true, 0U }, /* orientação padrão */
	{ STEP_COMMAND, CMD_DISPON, 0U, false, 0U },
};
#define INIT_STEP_COUNT (sizeof(init_steps) / sizeof(init_steps[0]))

enum init_state { INIT_OFF, INIT_RUNNING, INIT_READY, INIT_FAILED };
enum job_state { JOB_NONE, JOB_PENDING, JOB_DONE, JOB_FAILED };

static struct {
	enum init_state init;
	size_t init_step;        /* próximo passo da inicialização */
	int64_t init_wait_until; /* só segue quando now >= isto */
	enum job_state job;
	struct display_item items[DISPLAY_MAX_ITEMS];
	size_t item_count;
	size_t item;             /* item que está sendo pintado */
	unsigned int row;        /* próxima linha de pixels dentro do item */
	bool window_open;        /* a janela (CASET/RASET/RAMWR) do item já foi enviada? */
} disp;

/* ---- inicialização ---- */

static int run_step(const struct init_step *step)
{
	if (step->kind == STEP_RESET_ASSERT) {
		return display_hw_reset(true);
	}
	if (step->kind == STEP_RESET_RELEASE) {
		return display_hw_reset(false);
	}
	return display_hw_command(step->command, &step->param, step->has_param ? 1U : 0U);
}

/*
 * Executa os passos cujo momento chegou. Passos sem espera (wait_ms == 0)
 * encadeiam na MESMA chamada; os com espera devolvem o controle ao laço principal.
 */
static void init_run(int64_t now)
{
	while (disp.init == INIT_RUNNING && now >= disp.init_wait_until) {
		const struct init_step *step = &init_steps[disp.init_step];

		if (run_step(step) < 0) {
			disp.init = INIT_FAILED;
			return;
		}
		disp.init_wait_until = now + step->wait_ms;
		disp.init_step++;
		if (disp.init_step == INIT_STEP_COUNT) {
			disp.init = INIT_READY;
		}
	}
}

/* ---- desenho ---- */

/* Envia CASET, RASET e RAMWR: define o retângulo e abre a escrita de pixels. */
static int open_window(const struct display_item *item)
{
	uint16_t x1 = (uint16_t)(item->x + display_item_width(item) - 1U);
	uint16_t y1 = (uint16_t)(item->y + display_item_height(item) - 1U);
	const uint8_t columns[4] = { (uint8_t)(item->x >> 8), (uint8_t)item->x, (uint8_t)(x1 >> 8),
				     (uint8_t)x1 };
	const uint8_t rows[4] = { (uint8_t)(item->y >> 8), (uint8_t)item->y, (uint8_t)(y1 >> 8),
				  (uint8_t)y1 };

	if (display_hw_command(CMD_CASET, columns, sizeof(columns)) < 0 ||
	    display_hw_command(CMD_RASET, rows, sizeof(rows)) < 0) {
		return -EIO;
	}
	return display_hw_command(CMD_RAMWR, NULL, 0U);
}

/* Envia UMA linha de pixels do item atual (abrindo a janela se for a primeira). */
static int draw_one_row(void)
{
	const struct display_item *item = &disp.items[disp.item];
	uint8_t pixels[DISPLAY_ROW_BYTES_MAX];
	size_t length;

	if (!disp.window_open) {
		if (open_window(item) < 0) {
			return -EIO;
		}
		disp.window_open = true;
		disp.row = 0U;
	}
	length = display_render_row(item, disp.row, pixels);
	if (display_hw_data(pixels, length) < 0) {
		return -EIO;
	}
	disp.row++;
	if (disp.row >= display_item_height(item)) { /* terminou este retângulo */
		disp.window_open = false;
		disp.item++;
		if (disp.item >= disp.item_count) {
			disp.job = JOB_DONE;
		}
	}
	return 0;
}

static void draw_run(void)
{
	for (unsigned int i = 0U; i < DISPLAY_ROWS_PER_STEP && disp.job == JOB_PENDING; ++i) {
		if (draw_one_row() < 0) {
			disp.job = JOB_FAILED;
		}
	}
}

void display_service(int64_t now)
{
	if (disp.init == INIT_RUNNING) {
		init_run(now);
	}
	if (disp.job != JOB_PENDING) {
		return;
	}
	if (disp.init == INIT_FAILED) {
		disp.job = JOB_FAILED;
	} else if (disp.init == INIT_READY) {
		draw_run();
	}
}

/* ---- API ---- */

int display_init(void)
{
	int rc;

	memset(&disp, 0, sizeof(disp));
	rc = display_hw_init();
	if (rc < 0) {
		return rc; /* continua OFF: qualquer desenho devolve -ENODEV */
	}
	disp.init = INIT_RUNNING;
	return 0;
}

/* Verificações comuns a qualquer pedido de desenho. */
static int check_can_draw(void)
{
	if (disp.init == INIT_OFF) {
		return -ENODEV;
	}
	if (disp.init == INIT_FAILED) {
		return -EIO;
	}
	return disp.job == JOB_PENDING ? -EBUSY : 0;
}

/* Começa um desenho com `count` itens (já preenchidos em disp.items). */
static int begin_job(size_t count)
{
	if (count == 0U) {
		return -EINVAL;
	}
	disp.item_count = count;
	disp.item = 0U;
	disp.row = 0U;
	disp.window_open = false;
	disp.job = JOB_PENDING;
	return 0;
}

int display_draw_plate(const char plate[PARKING_PLATE_SIZE])
{
	int rc = check_can_draw();

	if (rc < 0) {
		return rc;
	}
	return begin_job(display_layout_plate(plate, disp.items));
}

int display_draw_charge(const char plate[PARKING_PLATE_SIZE], uint32_t amount_centavos)
{
	int rc = check_can_draw();

	if (rc < 0) {
		return rc;
	}
	return begin_job(display_layout_charge(plate, amount_centavos, disp.items));
}

enum equipment_result display_poll(int64_t now)
{
	display_service(now);
	if (disp.job == JOB_PENDING) {
		return EQUIPMENT_PENDING;
	}
	return disp.job == JOB_DONE ? EQUIPMENT_OK : EQUIPMENT_FAILED;
}

void display_cancel(void)
{
	/* Só linhas inteiras de pixels são enviadas, então parar aqui nunca deixa um
	 * pixel pela metade: o próximo CASET/RASET/RAMWR recomeça limpo. */
	disp.job = JOB_NONE;
	disp.window_open = false;
}

const struct payment_display_ops *display_ops(void)
{
	static const struct payment_display_ops ops = {
		.draw_plate = display_draw_plate,
		.draw_charge = display_draw_charge,
		.poll = display_poll,
		.cancel = display_cancel,
	};

	return &ops;
}
