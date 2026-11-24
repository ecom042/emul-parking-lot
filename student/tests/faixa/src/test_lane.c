/*
 * Máquina de estados da faixa (lane.c) com hw.h e ui.h falsos.
 *
 * Os falsos fazem o papel do Renode: os testes escolhem os níveis dos fins
 * de curso e do feixe, entregam linhas da câmera/terminal e amostras do ADC
 * e verificam o que a faixa mandou para os pinos e para as UARTs.
 * Os tempos são em ms virtuais, passados direto para as funções.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "hw.h"
#include "lane.h"
#include "sessions.h"
#include "ui.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, hw_motor, enum lane_id, enum motor_cmd);
FAKE_VALUE_FUNC(int, hw_lights, enum lane_id, bool, bool);
FAKE_VALUE_FUNC(int, hw_buzzer, enum lane_id, bool);
FAKE_VALUE_FUNC(bool, hw_limit_upper, enum lane_id);
FAKE_VALUE_FUNC(bool, hw_limit_lower, enum lane_id);
FAKE_VALUE_FUNC(bool, hw_beam, enum lane_id);
FAKE_VALUE_FUNC(int, hw_uart_send, enum uart_ch, const char *, size_t);
FAKE_VALUE_FUNC(int, ui_request_plate, const char *);
FAKE_VALUE_FUNC(int, ui_request_amount, uint32_t);

/* ---- Ambiente simulado -------------------------------------------------- */

static bool upper, lower, beam;
static enum motor_cmd motor;     /* último comando do motor */
static bool red, green, buzzer;  /* níveis atuais */
static int buzzer_pulses;        /* bordas de subida do buzzer */
static int green_on;             /* bordas de subida do verde */
static char last_tx[UART_COUNT][UART_FRAME_MAX]; /* último quadro de cada UART */
static int tx_count[UART_COUNT];
static int fail_motor_cmd = -1; /* comando do motor que deve falhar */

static bool upper_fake(enum lane_id l) { ARG_UNUSED(l); return upper; }
static bool lower_fake(enum lane_id l) { ARG_UNUSED(l); return lower; }
static bool beam_fake(enum lane_id l) { ARG_UNUSED(l); return beam; }

static int motor_fake(enum lane_id l, enum motor_cmd cmd)
{
	ARG_UNUSED(l);
	if ((int)cmd == fail_motor_cmd) {
		return -EIO;
	}
	motor = cmd;
	return 0;
}

static int lights_fake(enum lane_id l, bool r, bool g)
{
	ARG_UNUSED(l);
	green_on += (g && !green) ? 1 : 0;
	red = r;
	green = g;
	return 0;
}

static int buzzer_fake(enum lane_id l, bool on)
{
	ARG_UNUSED(l);
	buzzer_pulses += (on && !buzzer) ? 1 : 0;
	buzzer = on;
	return 0;
}

static int uart_fake(enum uart_ch ch, const char *buf, size_t len)
{
	size_t n = MIN(len, sizeof(last_tx[ch]) - 1);

	memcpy(last_tx[ch], buf, n);
	last_tx[ch][n] = '\0';
	tx_count[ch]++;
	return 0;
}

static struct lane lane;
static int64_t now;

static void before(void *f)
{
	ARG_UNUSED(f);
	RESET_FAKE(hw_motor);
	RESET_FAKE(hw_lights);
	RESET_FAKE(hw_buzzer);
	RESET_FAKE(hw_limit_upper);
	RESET_FAKE(hw_limit_lower);
	RESET_FAKE(hw_beam);
	RESET_FAKE(hw_uart_send);
	RESET_FAKE(ui_request_plate);
	RESET_FAKE(ui_request_amount);
	FFF_RESET_HISTORY();
	hw_motor_fake.custom_fake = motor_fake;
	hw_lights_fake.custom_fake = lights_fake;
	hw_buzzer_fake.custom_fake = buzzer_fake;
	hw_limit_upper_fake.custom_fake = upper_fake;
	hw_limit_lower_fake.custom_fake = lower_fake;
	hw_beam_fake.custom_fake = beam_fake;
	hw_uart_send_fake.custom_fake = uart_fake;

	upper = false;
	lower = true; /* cancela fechada */
	beam = false;
	motor = MOTOR_OFF;
	red = green = buzzer = false;
	buzzer_pulses = green_on = 0;
	memset(last_tx, 0, sizeof(last_tx));
	memset(tx_count, 0, sizeof(tx_count));
	fail_motor_cmd = -1;
	now = 0;
	sessions_reset();
	lane_module_reset();
}

ZTEST_SUITE(lane, NULL, NULL, before, NULL, NULL);

/* ---- Ajudantes ---------------------------------------------------------- */

static void advance(int64_t ms)
{
	now += ms;
	lane_tick(&lane, now);
}

/** Cria a faixa e espera a partida (sensores assentados). */
static void boot(enum lane_id id)
{
	lane_init(&lane, id, now);
	advance(STARTUP_SETTLE_MS);
	zassert_equal(lane.state, LANE_IDLE);
}

/** Responde à captura atual com a placa e a confiança dadas. */
static void reply_plate(const char *plate, int conf)
{
	char line[64];

	snprintf(line, sizeof(line), "+PLATE:%u,\"%s\",%d", (unsigned int)lane.capture_id, plate,
		 conf);
	lane_on_camera_line(&lane, line, now);
}

/** Carro chega e a câmera reconhece a placa: a faixa decide. */
static void arrive(const char *plate)
{
	lane_on_distance(&lane, 546, now);
	zassert_equal(lane.state, LANE_IDENTIFYING);
	reply_plate(plate, 93);
}

/** Braço chega ao fim de curso superior. */
static void reach_upper(void)
{
	lower = false;
	upper = true;
	advance(2000);
}

/** Carro passa: feixe ativo, depois livre, e distância acima de 1500 mm. */
static void pass_through(void)
{
	beam = true;
	advance(500);
	beam = false;
	advance(500);
	lane_on_distance(&lane, 1500, now);
}

/** Linha de resultado do terminal para a cobrança atual. */
static void reply_payment(const char *uuid, const char *status, uint32_t amount)
{
	char line[128];

	snprintf(line, sizeof(line), "P1 RESULT %s %s %u BRL", uuid, status,
		 (unsigned int)amount);
	lane_on_payment_line(&lane, line, now);
}

/** Saída até PAYING: placa com sessão, display pronto duas vezes. */
static void exit_until_paying(const char *plate, int64_t parked_ms)
{
	zassert_ok(sessions_add(plate, now));
	now += parked_ms;
	boot(LANE_EXIT);
	arrive(plate);
	zassert_equal(lane.state, LANE_SHOW_PLATE);
	lane_on_display_done(&lane, true, now);
	zassert_equal(lane.state, LANE_SHOW_AMOUNT);
	lane_on_display_done(&lane, true, now);
	zassert_equal(lane.state, LANE_PAYING);
}

/* ---- REQ-01: partida segura --------------------------------------------- */

ZTEST(lane, test_partida_saidas_seguras)
{
	lane_init(&lane, LANE_ENTRY, 0);
	zassert_equal(motor, MOTOR_OFF);
	zassert_true(red);
	zassert_false(green);
	zassert_false(buzzer);
	advance(STARTUP_SETTLE_MS - 1);
	zassert_equal(lane.state, LANE_STARTUP, "ainda assentando");
	advance(1);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_partida_sem_inferior_pede_assistencia)
{
	lower = false; /* braço a meio caminho */
	lane_init(&lane, LANE_ENTRY, 0);
	advance(STARTUP_SETTLE_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
	zassert_equal(motor, MOTOR_OFF, "nunca volta para casa sozinho");
	zassert_equal(buzzer_pulses, 1);
	/* O pulso dura 100 ms. */
	advance(BUZZER_PULSE_MS - 1);
	zassert_true(buzzer);
	advance(1);
	zassert_false(buzzer);
}

ZTEST(lane, test_fins_de_curso_contraditorios_travam)
{
	boot(LANE_ENTRY);
	upper = true; /* inferior e superior juntos */
	advance(10);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
	/* Mesmo que a falha suma e o carro saia, continua travada. */
	upper = false;
	advance(10);
	lane_on_distance(&lane, 4095, now);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(buzzer_pulses, 1, "um único pulso");
}

ZTEST(lane, test_ociosa_perde_o_inferior)
{
	boot(LANE_ENTRY);
	lower = false;
	advance(10);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
}

/* ---- REQ-02/03: reconhecimento ------------------------------------------ */

ZTEST(lane, test_chegada_pede_captura)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 683, now);
	zassert_equal(lane.state, LANE_IDLE, "683 ainda não é chegada");
	lane_on_distance(&lane, 682, now);
	zassert_equal(lane.state, LANE_IDENTIFYING);
	zassert_str_equal(last_tx[UART_CAM_ENTRY], "AT+CAPTURE=1\r\n");
	zassert_true(red);
}

ZTEST(lane, test_saida_usa_a_camera_da_saida)
{
	boot(LANE_EXIT);
	lane_on_distance(&lane, 0, now);
	zassert_equal(tx_count[UART_CAM_EXIT], 1);
	zassert_equal(tx_count[UART_CAM_ENTRY], 0);
}

ZTEST(lane, test_placa_aceita_abre)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	zassert_equal(lane.state, LANE_OPENING);
	zassert_equal(motor, MOTOR_OPEN);
	zassert_false(green, "verde só no fim de curso superior");
}

ZTEST(lane, test_confianca_85_e_aceita)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 0, now);
	reply_plate("ABC1D23", 85);
	zassert_equal(lane.state, LANE_OPENING);
}

ZTEST(lane, test_ignora_ok_erro_e_outro_id)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 0, now);
	lane_on_camera_line(&lane, "OK", now);
	lane_on_camera_line(&lane, "ERROR", now);
	lane_on_camera_line(&lane, "+PLATE:99,\"ABC1D23\",93", now);
	lane_on_camera_line(&lane, "+PLATE:1,\"ABC1D2\",93", now);
	zassert_equal(lane.state, LANE_IDENTIFYING);
	zassert_equal(lane.attempts, 1);
}

ZTEST(lane, test_confianca_62_79_93)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 0, now);
	reply_plate("ABC1D23", 62);
	zassert_str_equal(last_tx[UART_CAM_ENTRY], "AT+CAPTURE=2\r\n", "ID novo");
	reply_plate("ABC1D23", 79);
	zassert_str_equal(last_tx[UART_CAM_ENTRY], "AT+CAPTURE=3\r\n");
	reply_plate("ABC1D23", 93);
	zassert_equal(lane.state, LANE_OPENING);
	zassert_equal(tx_count[UART_CAM_ENTRY], 3);
}

ZTEST(lane, test_tres_confiancas_baixas_assistencia_rearmavel)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 0, now);
	reply_plate("ABC1D23", 62);
	reply_plate("ABC1D23", 79);
	reply_plate("ABC1D23", 84);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_false(lane.latched);
	zassert_equal(tx_count[UART_CAM_ENTRY], 3, "sem quarta tentativa");
	/* Carro parado continua presente: nada de nova captura. */
	lane_on_distance(&lane, 546, now);
	zassert_equal(lane.state, LANE_ASSIST);
	/* Feixe ocupado também impede o rearme. */
	beam = true;
	lane_on_distance(&lane, 2000, now);
	zassert_equal(lane.state, LANE_ASSIST);
	/* Carro foi embora com o feixe livre: rearma. */
	beam = false;
	lane_on_distance(&lane, 1025, now);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_rearme_exige_cancela_fechada)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 0, now);
	for (int i = 0; i < 3; i++) {
		reply_plate("ABC1D23", 10);
	}
	upper = true;
	lower = false;
	lane_on_distance(&lane, 2000, now);
	zassert_equal(lane.state, LANE_ASSIST);
	upper = true;
	lower = true; /* contraditório: também não rearma */
	lane_on_distance(&lane, 2000, now);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_prazo_da_camera)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 0, now);
	advance(CAMERA_ATTEMPT_MS - 1);
	zassert_equal(lane.attempts, 1);
	advance(1);
	zassert_equal(lane.attempts, 2);
	advance(CAMERA_ATTEMPT_MS);
	advance(CAMERA_ATTEMPT_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(tx_count[UART_CAM_ENTRY], 3);
	/* Resposta atrasada da última tentativa não autoriza nada. */
	reply_plate("ABC1D23", 93);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_carro_sai_durante_identificacao)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 0, now);
	lane_on_distance(&lane, 1024, now);
	zassert_equal(lane.state, LANE_IDENTIFYING, "1024 ainda está presente");
	lane_on_distance(&lane, 1025, now);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(buzzer_pulses, 0);
	reply_plate("ABC1D23", 93);
	zassert_equal(lane.state, LANE_IDLE, "resposta da tentativa abandonada");
}

ZTEST(lane, test_identificando_perde_o_inferior)
{
	boot(LANE_ENTRY);
	lane_on_distance(&lane, 0, now);
	lower = false;
	advance(10);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
}

/* ---- REQ-04: sessões ---------------------------------------------------- */

ZTEST(lane, test_entrada_de_placa_ja_estacionada)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_false(lane.latched);
	zassert_equal(motor, MOTOR_OFF);
}

ZTEST(lane, test_entrada_com_lotado)
{
	char plate[8];

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		snprintf(plate, sizeof(plate), "AAA%04d", i);
		zassert_ok(sessions_add(plate, 0));
	}
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(motor, MOTOR_OFF);
}

ZTEST(lane, test_saida_sem_sessao)
{
	boot(LANE_EXIT);
	arrive("ABC1D23");
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(ui_request_plate_fake.call_count, 0, "não cobra ninguém");
}

/* ---- REQ-06/07/08: cancela, passagem e fechamento ----------------------- */

ZTEST(lane, test_ciclo_completo_da_entrada)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	reach_upper();
	zassert_equal(lane.state, LANE_OPEN);
	zassert_equal(motor, MOTOR_OFF, "motor desligado antes do verde");
	zassert_true(green);
	zassert_false(red);
	zassert_false(sessions_contains("ABC1D23"), "abrir não cria sessão");

	pass_through();
	zassert_true(sessions_contains("ABC1D23"), "passagem completa cria a sessão");
	zassert_equal(lane.state, LANE_CLOSING);
	zassert_false(green);
	zassert_true(red);
	zassert_equal(motor, MOTOR_CLOSE);

	upper = false;
	lower = true;
	advance(2000);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(motor, MOTOR_OFF);
	zassert_equal(green_on, 1);
}

ZTEST(lane, test_passagem_exige_feixe_e_distancia)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	reach_upper();
	lane_on_distance(&lane, 2000, now);
	zassert_equal(lane.state, LANE_OPEN, "longe, mas o feixe nunca foi ativado");
	beam = true;
	advance(10);
	lane_on_distance(&lane, 2000, now);
	zassert_equal(lane.state, LANE_OPEN, "feixe ainda ativo");
	beam = false;
	advance(10);
	lane_on_distance(&lane, 900, now);
	zassert_equal(lane.state, LANE_OPEN, "feixe livre, mas carro perto");
	lane_on_distance(&lane, 1025, now);
	zassert_equal(lane.state, LANE_CLOSING);
}

ZTEST(lane, test_travamento_ao_abrir)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	advance(MOTOR_BUDGET_MS - 1);
	zassert_equal(motor, MOTOR_OPEN);
	advance(1);
	zassert_equal(motor, MOTOR_OFF);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
	zassert_equal(green_on, 0);
}

ZTEST(lane, test_falha_do_motor_ao_ligar)
{
	boot(LANE_ENTRY);
	fail_motor_cmd = MOTOR_OPEN;
	arrive("ABC1D23");
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
}

ZTEST(lane, test_falha_do_motor_ao_parar_no_superior)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	fail_motor_cmd = MOTOR_OFF;
	reach_upper();
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_false(green, "sem confirmar o motor parado, nada de verde");
}

ZTEST(lane, test_aberta_perde_o_superior)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	reach_upper();
	upper = false;
	advance(10);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
	zassert_false(green);
}

ZTEST(lane, test_prazo_de_passagem)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	reach_upper();
	beam = true; /* obstrução antes de fechar: o feixe nunca libera */
	advance(PASSAGE_BUDGET_MS - 1);
	zassert_equal(lane.state, LANE_OPEN);
	advance(1);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched, "aberta: travada");
	zassert_equal(motor, MOTOR_OFF, "não fecha sobre o feixe");
}

ZTEST(lane, test_obstrucao_no_fechamento)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	reach_upper();
	pass_through();
	upper = false;
	advance(500);
	beam = true;
	advance(1);
	zassert_equal(motor, MOTOR_OFF);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
	/* Feixe livre de novo: não reverte nem religa. */
	beam = false;
	advance(1000);
	lane_on_distance(&lane, 4095, now);
	zassert_equal(motor, MOTOR_OFF);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_inferior_ausente_no_fechamento)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	reach_upper();
	pass_through();
	upper = false; /* braço desce, mas o inferior nunca acende */
	advance(MOTOR_BUDGET_MS);
	zassert_equal(motor, MOTOR_OFF);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
}

ZTEST(lane, test_falha_do_motor_ao_parar_no_inferior)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	reach_upper();
	pass_through();
	fail_motor_cmd = MOTOR_OFF;
	upper = false;
	lower = true;
	advance(2000);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_falha_do_motor_ao_fechar)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	reach_upper();
	fail_motor_cmd = MOTOR_CLOSE;
	pass_through();
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
}

/* ---- REQ-05/12: pagamento na saída -------------------------------------- */

ZTEST(lane, test_saida_paga_e_sai)
{
	exit_until_paying("ABC1D23", 2500);
	/* Tarifa congelada: com unidade de 1 s, 2,5 s = 400; com 60 s, 200. */
	uint32_t expected = (CONFIG_PARKING_TARIFF_UNIT_MS == 1000) ? 400 : 200;

	zassert_equal(ui_request_plate_fake.call_count, 1);
	zassert_str_equal(ui_request_plate_fake.arg0_val, "ABC1D23");
	zassert_equal(ui_request_amount_fake.arg0_val, expected);
	zassert_equal(lane.amount, expected);

	char charge[128];

	snprintf(charge, sizeof(charge), "P1 CHARGE %s ABC1D23 %u BRL\n", lane.uuid,
		 (unsigned int)expected);
	zassert_str_equal(last_tx[UART_PAYMENT], charge);

	reply_payment(lane.uuid, "PAID", expected);
	zassert_equal(lane.state, LANE_OPENING);
	zassert_false(lane.uuid_active, "um PAID só autoriza uma vez");
	reach_upper();
	zassert_true(sessions_contains("ABC1D23"), "pagar não encerra a sessão");
	pass_through();
	zassert_false(sessions_contains("ABC1D23"), "passagem encerra a sessão");
	zassert_equal(lane.state, LANE_CLOSING);
}

ZTEST(lane, test_paid_com_outro_valor_uuid_antigo_ou_malformado)
{
	exit_until_paying("ABC1D23", 0);
	reply_payment(lane.uuid, "PAID", lane.amount + 100);
	reply_payment("00000000-0000-4000-8000-000000000000", "PAID", lane.amount);
	lane_on_payment_line(&lane, "P1 RESULT broken", now);
	lane_on_payment_line(&lane, "P1 ERROR TIMEOUT", now);
	zassert_equal(lane.state, LANE_PAYING);
	zassert_equal(motor, MOTOR_OFF);
}

ZTEST(lane, test_reenvio_e_desistencia)
{
	exit_until_paying("ABC1D23", 0);
	char first[128];

	strcpy(first, last_tx[UART_PAYMENT]);
	advance(PAYMENT_SEND_MS);
	zassert_str_equal(last_tx[UART_PAYMENT], first, "mesmo UUID e valor");
	advance(PAYMENT_SEND_MS);
	zassert_equal(tx_count[UART_PAYMENT], 3);
	advance(PAYMENT_SEND_MS - 1);
	zassert_equal(lane.state, LANE_PAYING);
	advance(1);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_false(lane.uuid_active, "UUID invalidado");
	char cancel[64];

	snprintf(cancel, sizeof(cancel), "P1 CANCEL %s\n", lane.uuid);
	zassert_str_equal(last_tx[UART_PAYMENT], cancel);
	/* Um PAID que chega depois não abre. */
	reply_payment(lane.uuid, "PAID", lane.amount);
	zassert_equal(motor, MOTOR_OFF);
}

ZTEST(lane, test_pagamento_atrasado_ainda_vale)
{
	exit_until_paying("ABC1D23", 0);
	advance(PAYMENT_SEND_MS + 500); /* um reenvio, depois a resposta */
	reply_payment(lane.uuid, "PAID", lane.amount);
	zassert_equal(lane.state, LANE_OPENING);
}

ZTEST(lane, test_cancelado_pelo_terminal)
{
	exit_until_paying("ABC1D23", 0);
	reply_payment(lane.uuid, "CANCELLED", lane.amount);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_false(lane.uuid_active);
	zassert_equal(motor, MOTOR_OFF);
	zassert_not_null(strstr(last_tx[UART_PAYMENT], "P1 CANCEL"));
}

ZTEST(lane, test_carro_sai_durante_pagamento)
{
	exit_until_paying("ABC1D23", 0);
	lane_on_distance(&lane, 2000, now);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_not_null(strstr(last_tx[UART_PAYMENT], "P1 CANCEL"));
	/* Carro longe e feixe livre: a assistência rearma. */
	lane_on_distance(&lane, 2000, now);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_true(sessions_contains("ABC1D23"), "não pagou: continua estacionado");
}

ZTEST(lane, test_carro_sai_enquanto_o_display_desenha)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	boot(LANE_EXIT);
	arrive("ABC1D23");
	lane_on_distance(&lane, 2000, now);
	zassert_equal(lane.state, LANE_ASSIST);
	/* A etapa que termina depois é ignorada. */
	lane_on_display_done(&lane, true, now);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(tx_count[UART_PAYMENT], 0, "nenhuma cobrança enviada");
}

ZTEST(lane, test_display_falha_ou_demora)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	boot(LANE_EXIT);
	arrive("ABC1D23");
	lane_on_display_done(&lane, false, now);
	zassert_equal(lane.state, LANE_ASSIST);

	before(NULL);
	zassert_ok(sessions_add("ABC1D23", 0));
	boot(LANE_EXIT);
	arrive("ABC1D23");
	advance(DISPLAY_BUDGET_MS);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_pedido_ao_display_recusado)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	ui_request_plate_fake.return_val = -ENOMSG;
	boot(LANE_EXIT);
	arrive("ABC1D23");
	zassert_equal(lane.state, LANE_ASSIST);

	before(NULL);
	zassert_ok(sessions_add("ABC1D23", 0));
	ui_request_amount_fake.return_val = -ENOMSG;
	boot(LANE_EXIT);
	arrive("ABC1D23");
	lane_on_display_done(&lane, true, now);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_tarifa_acima_de_9999_unidades)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	now = 10000LL * CONFIG_PARKING_TARIFF_UNIT_MS + 1;
	boot(LANE_EXIT);
	arrive("ABC1D23");
	lane_on_display_done(&lane, true, now);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(ui_request_amount_fake.call_count, 0);
}

ZTEST(lane, test_sessao_sumiu_antes_da_tarifa)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	boot(LANE_EXIT);
	arrive("ABC1D23");
	zassert_ok(sessions_remove("ABC1D23"));
	lane_on_display_done(&lane, true, now);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(tx_count[UART_PAYMENT], 0);
}

ZTEST(lane, test_pagamento_fora_de_hora_ignorado)
{
	boot(LANE_EXIT);
	reply_payment("00000000-0000-4000-8000-000000000000", "PAID", 200);
	lane_on_display_done(&lane, true, now);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_pagando_perde_o_inferior)
{
	exit_until_paying("ABC1D23", 0);
	lower = false;
	advance(10);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
	zassert_false(lane.uuid_active, "cancelou a cobrança");
}

ZTEST(lane, test_uuid_novo_por_tentativa)
{
	char first[UUID_LEN + 1];

	exit_until_paying("ABC1D23", 0);
	strcpy(first, lane.uuid);
	lane_on_distance(&lane, 2000, now); /* desiste */
	lane_on_distance(&lane, 2000, now); /* rearma */
	arrive("ABC1D23");
	lane_on_display_done(&lane, true, now);
	lane_on_display_done(&lane, true, now);
	zassert_equal(lane.state, LANE_PAYING);
	zassert_true(strcmp(first, lane.uuid) != 0);
}

/* ---- Outros ------------------------------------------------------------- */

ZTEST(lane, test_falha_do_adc_trava)
{
	boot(LANE_ENTRY);
	lane_fault(&lane, now);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_true(lane.latched);
	lane_on_distance(&lane, 4095, now);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_distancia_ignorada_ao_mover)
{
	boot(LANE_ENTRY);
	arrive("ABC1D23");
	lane_on_distance(&lane, 0, now);
	lane_on_distance(&lane, 4095, now);
	zassert_equal(lane.state, LANE_OPENING);
	lane_init(&lane, LANE_ENTRY, now);
	lane_on_distance(&lane, 0, now);
	zassert_equal(lane.state, LANE_STARTUP);
}

ZTEST(lane, test_nomes_dos_estados)
{
	zassert_str_equal(lane_state_name(LANE_IDLE), "IDLE");
	zassert_str_equal(lane_state_name(LANE_ASSIST), "ASSIST");
}
