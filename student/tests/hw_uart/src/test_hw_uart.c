/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Camada de UART (hw_uart.c) com os drivers falsos (FFF). O teste faz o papel
 * do hardware: chama a ISR que hw_uart_init registrou, diz se ha bytes no
 * FIFO de recepcao, injeta esses bytes e limita quantos bytes o FIFO de
 * transmissao aceita por vez. Os analisadores AT e P1 sao os reais.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "fakes.h"
#include "hw_uart.h"

#define UUID_OK "3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1"

/* Chama a ISR da porta como o hardware faria, com as flags dadas. */
static void isr(enum hw_uart_port port, int rx_ready, int tx_ready)
{
	fake_uart_irq_rx_ready_fake.return_val = rx_ready;
	fake_uart_irq_tx_ready_fake.return_val = tx_ready;
	zassert_not_null(fake_isr[port], "ISR da porta %d nao registrada", port);
	fake_isr[port](fake_port_devs[port], fake_isr_data[port]);
}

/* Poe bytes no FIFO de recepcao e dispara a ISR de recepcao. */
static void recebe(enum hw_uart_port port, const char *bytes)
{
	fake_rx_src = (const uint8_t *)bytes;
	fake_rx_len = (int)strlen(bytes);
	isr(port, 1, 0);
	zassert_equal(fake_rx_len, 0, "a ISR deve drenar o FIFO de recepcao");
}

/* Zera os falsos sem perder as ISRs registradas por hw_uart_init. */
static void zera_falsos_mantendo_isr(void)
{
	uart_irq_callback_user_data_t cb[FAKE_PORTS];
	void *data[FAKE_PORTS];

	memcpy(cb, fake_isr, sizeof(cb));
	memcpy(data, fake_isr_data, sizeof(data));
	fakes_reset();
	memcpy(fake_isr, cb, sizeof(cb));
	memcpy(fake_isr_data, data, sizeof(data));
}

/*
 * Antes de cada teste: liga as portas e termina qualquer envio que um teste
 * anterior tenha deixado pela metade (hw_uart_init nao mexe no TX).
 */
static void preparar(void *fixture)
{
	ARG_UNUSED(fixture);
	fakes_reset();
	zassert_equal(hw_uart_init(), 0);
	for (int i = 0; i < FAKE_PORTS; i++) {
		isr((enum hw_uart_port)i, 0, 1);
	}
	zera_falsos_mantendo_isr();
}

ZTEST_SUITE(hw_uart, NULL, NULL, preparar, NULL, NULL);

/* ---- hw_uart_init ------------------------------------------------------ */

ZTEST(hw_uart, test_init_registra_isr_por_porta)
{
	fakes_reset();

	zassert_equal(hw_uart_init(), 0);
	zassert_equal(fake_device_is_ready_fake.call_count, 3);
	zassert_equal(fake_uart_irq_callback_user_data_set_fake.call_count, 3);
	zassert_equal(fake_uart_irq_rx_enable_fake.call_count, 3);
	for (int i = 0; i < FAKE_PORTS; i++) {
		zassert_equal_ptr(fake_uart_irq_rx_enable_fake.arg0_history[i],
				  fake_port_devs[i], "rx ligado na porta errada");
		zassert_equal_ptr(fake_uart_irq_callback_user_data_set_fake.arg1_history[i],
				  fake_isr[0], "todas as portas usam a mesma ISR");
		zassert_not_null(fake_isr[i]);
		zassert_not_null(fake_isr_data[i]);
	}
	zassert_not_equal(fake_isr_data[0], fake_isr_data[1], "cada porta tem seu estado");
	zassert_not_equal(fake_isr_data[1], fake_isr_data[2], "cada porta tem seu estado");
	zassert_equal(fake_uart_irq_tx_enable_fake.call_count, 0, "TX so liga ao enviar");
}

ZTEST(hw_uart, test_init_dispositivo_nao_pronto)
{
	static bool prontos[] = { true, false };

	fakes_reset();
	SET_RETURN_SEQ(fake_device_is_ready, prontos, ARRAY_SIZE(prontos));

	zassert_equal(hw_uart_init(), -ENODEV);
	zassert_equal(fake_device_is_ready_fake.call_count, 2, "para na porta que falhou");
	zassert_equal_ptr(fake_device_is_ready_fake.arg0_history[1], fake_port_devs[1]);
	zassert_equal(fake_uart_irq_callback_user_data_set_fake.call_count, 1);
	zassert_equal(fake_uart_irq_rx_enable_fake.call_count, 1,
		      "so a porta pronta liga a recepcao");
}

ZTEST(hw_uart, test_init_erro_no_callback)
{
	fakes_reset();
	fake_uart_irq_callback_user_data_set_fake.custom_fake = NULL;
	fake_uart_irq_callback_user_data_set_fake.return_val = -ENOTSUP;

	zassert_equal(hw_uart_init(), -ENODEV);
	zassert_equal(fake_uart_irq_callback_user_data_set_fake.call_count, 1);
	zassert_equal(fake_uart_irq_rx_enable_fake.call_count, 0,
		      "sem ISR, a recepcao nao pode ser ligada");
}

/* ---- hw_uart_send ------------------------------------------------------ */

ZTEST(hw_uart, test_send_argumentos_invalidos)
{
	zassert_equal(hw_uart_send(HW_UART_PORT_COUNT, "AT"), -EINVAL);
	zassert_equal(hw_uart_send((enum hw_uart_port)99, "AT"), -EINVAL);
	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, NULL), -EINVAL);
	zassert_equal(fake_uart_irq_tx_enable_fake.call_count, 0);
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 0);
}

ZTEST(hw_uart, test_send_camera_termina_em_crlf)
{
	static const char esperado[] = "AT+CAPTURE=17\r\n";

	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, "AT+CAPTURE=17"), 0);

	/* Desliga o TX antes de mexer no buffer e liga depois. */
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 1);
	zassert_equal(fake_uart_irq_tx_enable_fake.call_count, 1);
	zassert_equal_ptr(fake_uart_irq_tx_enable_fake.arg0_val,
			  fake_port_devs[HW_UART_ENTRY_CAMERA]);
	zassert_equal(fff.call_history[0], (void *)fake_uart_irq_tx_disable);
	zassert_equal(fff.call_history[1], (void *)fake_uart_irq_tx_enable);
	zassert_equal(fake_uart_fifo_fill_fake.call_count, 0, "quem enche o FIFO e a ISR");

	isr(HW_UART_ENTRY_CAMERA, 0, 1);
	zassert_equal(fake_uart_fifo_fill_fake.call_count, 1);
	zassert_equal(fake_uart_fifo_fill_fake.arg2_val, (int)strlen(esperado));
	zassert_equal(fake_tx_sink_len, (int)strlen(esperado));
	zassert_mem_equal(fake_tx_sink, esperado, strlen(esperado));
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 2, "fim do envio desliga o TX");

	/* Envio terminado: a porta aceita outra linha. */
	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, "AT"), 0);
}

ZTEST(hw_uart, test_send_camera_saida_usa_a_propria_porta)
{
	zassert_equal(hw_uart_send(HW_UART_EXIT_CAMERA, "AT+CAPTURE=5"), 0);
	zassert_equal_ptr(fake_uart_irq_tx_enable_fake.arg0_val,
			  fake_port_devs[HW_UART_EXIT_CAMERA]);

	/* A ISR da entrada nao tem nada a enviar. */
	isr(HW_UART_ENTRY_CAMERA, 0, 1);
	zassert_equal(fake_uart_fifo_fill_fake.call_count, 0);

	isr(HW_UART_EXIT_CAMERA, 0, 1);
	zassert_equal(fake_uart_fifo_fill_fake.call_count, 1);
	zassert_equal_ptr(fake_uart_fifo_fill_fake.arg0_val,
			  fake_port_devs[HW_UART_EXIT_CAMERA]);
	zassert_mem_equal(fake_tx_sink, "AT+CAPTURE=5\r\n", 14);
	zassert_equal(fake_tx_sink_len, 14);
}

ZTEST(hw_uart, test_send_terminal_termina_so_em_lf)
{
	static const char linha[] = "P1 CHARGE " UUID_OK " 400 BRL";

	zassert_equal(hw_uart_send(HW_UART_PAYMENT, linha), 0);
	isr(HW_UART_PAYMENT, 0, 1);
	zassert_equal(fake_tx_sink_len, (int)strlen(linha) + 1);
	zassert_mem_equal(fake_tx_sink, linha, strlen(linha));
	zassert_equal(fake_tx_sink[strlen(linha)], '\n');
	zassert_not_equal(fake_tx_sink[strlen(linha) - 1], '\r', "terminal nao usa CR");
}

ZTEST(hw_uart, test_send_linha_longa)
{
	char linha[FRAME_MAX + 1];

	/* Camera: 126 + CR LF = 128 cabe; 127 + CR LF nao cabe. */
	memset(linha, 'A', sizeof(linha));
	linha[FRAME_MAX - 1] = '\0';
	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, linha), -EMSGSIZE);
	zassert_equal(fake_uart_irq_tx_enable_fake.call_count, 0);
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 0);

	linha[FRAME_MAX - 2] = '\0';
	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, linha), 0);
	isr(HW_UART_ENTRY_CAMERA, 0, 1);
	zassert_equal(fake_tx_sink_len, FRAME_MAX);
	zassert_mem_equal(&fake_tx_sink[FRAME_MAX - 2], "\r\n", 2);

	/* Terminal: 127 + LF = 128 cabe; 128 + LF nao cabe. */
	memset(linha, 'B', sizeof(linha));
	linha[FRAME_MAX] = '\0';
	zassert_equal(hw_uart_send(HW_UART_PAYMENT, linha), -EMSGSIZE);
	linha[FRAME_MAX - 1] = '\0';
	zassert_equal(hw_uart_send(HW_UART_PAYMENT, linha), 0);
}

ZTEST(hw_uart, test_send_ocupado_devolve_ebusy)
{
	zassert_equal(hw_uart_send(HW_UART_PAYMENT, "P1 PING"), 0);
	zassert_equal(hw_uart_send(HW_UART_PAYMENT, "P1 OUTRA"), -EBUSY);
	zassert_equal(fake_uart_irq_tx_enable_fake.call_count, 1, "o 2o envio nao mexe no TX");
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 1);

	/* Outra porta nao esta ocupada. */
	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, "AT"), 0);

	/* A linha recusada nao estragou a que esta saindo. */
	isr(HW_UART_PAYMENT, 0, 1);
	zassert_equal(fake_tx_sink_len, 8);
	zassert_mem_equal(fake_tx_sink, "P1 PING\n", 8);
	zassert_equal(hw_uart_send(HW_UART_PAYMENT, "P1 OUTRA"), 0);
}

/* ---- ISR de transmissao ------------------------------------------------ */

ZTEST(hw_uart, test_isr_esvazia_tx_em_pedacos)
{
	static const char esperado[] = "AT+CAPTURE=17\r\n"; /* 15 bytes */

	fake_tx_fill_max = 4;
	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, "AT+CAPTURE=17"), 0);

	/* Sem TX pronto, a ISR nao toca no FIFO. */
	isr(HW_UART_ENTRY_CAMERA, 0, 0);
	zassert_equal(fake_uart_fifo_fill_fake.call_count, 0);

	for (int i = 0; i < 3; i++) {
		isr(HW_UART_ENTRY_CAMERA, 0, 1);
		zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 1,
			      "nao desliga o TX com bytes pendentes");
		zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, "AT"), -EBUSY);
	}
	isr(HW_UART_ENTRY_CAMERA, 0, 1);
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 2, "ultimo pedaco desliga o TX");

	/* Cada chamada pede o que falta, a partir de onde parou. */
	zassert_equal(fake_uart_fifo_fill_fake.call_count, 4);
	zassert_equal(fake_uart_fifo_fill_fake.arg2_history[0], 15);
	zassert_equal(fake_uart_fifo_fill_fake.arg2_history[1], 11);
	zassert_equal(fake_uart_fifo_fill_fake.arg2_history[2], 7);
	zassert_equal(fake_uart_fifo_fill_fake.arg2_history[3], 3);
	zassert_equal(fake_uart_fifo_fill_fake.arg1_history[1] -
		      fake_uart_fifo_fill_fake.arg1_history[0], 4);
	zassert_equal(fake_tx_sink_len, 15);
	zassert_mem_equal(fake_tx_sink, esperado, 15);

	/* Envio terminado: TX pronto de novo nao enche nada. */
	isr(HW_UART_ENTRY_CAMERA, 0, 1);
	zassert_equal(fake_uart_fifo_fill_fake.call_count, 4);
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 2);
}

ZTEST(hw_uart, test_isr_fifo_cheio_nao_avanca)
{
	fake_tx_fill_max = 0;
	zassert_equal(hw_uart_send(HW_UART_PAYMENT, "P1 PING"), 0);

	isr(HW_UART_PAYMENT, 0, 1);
	zassert_equal(fake_uart_fifo_fill_fake.call_count, 1);
	zassert_equal(fake_tx_sink_len, 0);
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 1, "envio continua pendente");
	zassert_equal(hw_uart_send(HW_UART_PAYMENT, "P1 PING"), -EBUSY);

	/* FIFO com espaco: envia a linha inteira, desde o primeiro byte. */
	fake_tx_fill_max = 256;
	isr(HW_UART_PAYMENT, 0, 1);
	zassert_equal(fake_uart_fifo_fill_fake.arg2_val, 8);
	zassert_mem_equal(fake_tx_sink, "P1 PING\n", 8);
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 2);
}

ZTEST(hw_uart, test_isr_fifo_fill_com_erro_nao_avanca)
{
	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, "AT"), 0);

	fake_uart_fifo_fill_fake.custom_fake = NULL;
	fake_uart_fifo_fill_fake.return_val = -EIO;
	isr(HW_UART_ENTRY_CAMERA, 0, 1);
	zassert_equal(fake_uart_irq_tx_disable_fake.call_count, 1, "erro nao conta como enviado");
	zassert_equal(hw_uart_send(HW_UART_ENTRY_CAMERA, "AT"), -EBUSY);
}

/* ---- Recepcao ---------------------------------------------------------- */

ZTEST(hw_uart, test_isr_sem_rx_pronto_nao_le)
{
	struct at_plate out = { 0 };

	fake_rx_src = (const uint8_t *)"+PLATE:17,\"ABC1D23\",93\r\n";
	fake_rx_len = 24;
	isr(HW_UART_ENTRY_CAMERA, 0, 0);
	zassert_equal(fake_uart_irq_update_fake.call_count, 1);
	zassert_equal(fake_uart_fifo_read_fake.call_count, 0);
	zassert_false(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
}

ZTEST(hw_uart, test_poll_plate_entrega_quadro_valido)
{
	struct at_plate out = { 0 };

	recebe(HW_UART_ENTRY_CAMERA, "+PLATE:17,\"ABC1D23\",93\r\n");
	/* 24 bytes em pedacos de 16: 16, 8 e a leitura vazia que para o laco. */
	zassert_equal(fake_uart_fifo_read_fake.call_count, 3);
	zassert_equal(fake_uart_fifo_read_fake.arg2_history[0], 16);

	zassert_true(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	zassert_equal(out.id, 17);
	zassert_str_equal(out.plate, "ABC1D23");
	zassert_equal(out.confidence, 93);

	/* Quadro consumido; a outra camera nao recebeu nada. */
	zassert_false(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	zassert_false(hw_uart_poll_plate(HW_UART_EXIT_CAMERA, &out));
}

ZTEST(hw_uart, test_poll_plate_camera_da_saida)
{
	struct at_plate out = { 0 };

	recebe(HW_UART_EXIT_CAMERA, "+PLATE:5,\"XYZ9K88\",90\r\n");
	zassert_false(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	zassert_true(hw_uart_poll_plate(HW_UART_EXIT_CAMERA, &out));
	zassert_equal(out.id, 5);
	zassert_str_equal(out.plate, "XYZ9K88");
}

ZTEST(hw_uart, test_poll_plate_argumentos_invalidos)
{
	struct at_plate out = { 0 };

	recebe(HW_UART_ENTRY_CAMERA, "+PLATE:17,\"ABC1D23\",93\r\n");
	zassert_false(hw_uart_poll_plate(HW_UART_PAYMENT, &out), "terminal nao e camera");
	zassert_false(hw_uart_poll_plate(HW_UART_PORT_COUNT, &out));
	zassert_false(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, NULL));
	zassert_equal(out.id, 0);

	/* As chamadas recusadas nao consumiram o quadro. */
	zassert_true(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	zassert_equal(out.id, 17);
}

ZTEST(hw_uart, test_poll_plate_ignora_linha_invalida)
{
	struct at_plate out = { 0 };

	recebe(HW_UART_ENTRY_CAMERA, "OK\r\n+PLATE:22,\"abc1d23\",93\r\n");
	zassert_false(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	zassert_equal(out.id, 0);
}

ZTEST(hw_uart, test_dois_quadros_no_buffer)
{
	struct at_plate out = { 0 };

	recebe(HW_UART_ENTRY_CAMERA,
	       "+PLATE:17,\"ABC1D23\",93\r\n+PLATE:18,\"DEF4G56\",88\r\n");

	zassert_true(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	zassert_equal(out.id, 17);
	zassert_str_equal(out.plate, "ABC1D23");
	zassert_true(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	zassert_equal(out.id, 18);
	zassert_str_equal(out.plate, "DEF4G56");
	zassert_equal(out.confidence, 88);
	zassert_false(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
}

ZTEST(hw_uart, test_quadro_em_duas_interrupcoes)
{
	struct at_plate out = { 0 };

	recebe(HW_UART_ENTRY_CAMERA, "+PLATE:30,\"ABC");
	zassert_false(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	recebe(HW_UART_ENTRY_CAMERA, "1D23\",93\r\n");
	zassert_true(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &out));
	zassert_equal(out.id, 30);
}

ZTEST(hw_uart, test_poll_payment_entrega_resultado)
{
	struct p1_result out = { 0 };

	recebe(HW_UART_PAYMENT, "P1 RESULT " UUID_OK " PAID 400 BRL\n");

	zassert_false(hw_uart_poll_payment(NULL));
	zassert_true(hw_uart_poll_payment(&out), "NULL nao consumiu o quadro");
	zassert_equal(out.status, P1_STATUS_PAID);
	zassert_equal(out.cents, 400);
	zassert_str_equal(out.uuid, UUID_OK);
	zassert_false(hw_uart_poll_payment(&out));
}

ZTEST(hw_uart, test_poll_payment_dois_quadros)
{
	struct p1_result out = { 0 };

	recebe(HW_UART_PAYMENT,
	       "P1 ERROR FRAME\nP1 RESULT " UUID_OK " CANCELLED 400 BRL\n");

	zassert_true(hw_uart_poll_payment(&out));
	zassert_equal(out.status, P1_STATUS_ERROR);
	zassert_equal(out.error, P1_ERROR_FRAME);
	zassert_true(hw_uart_poll_payment(&out));
	zassert_equal(out.status, P1_STATUS_CANCELLED);
	zassert_str_equal(out.uuid, UUID_OK);
	zassert_false(hw_uart_poll_payment(&out));

	/* A camera nao ve os bytes do terminal. */
	zassert_false(hw_uart_poll_plate(HW_UART_ENTRY_CAMERA, &(struct at_plate){ 0 }));
}
