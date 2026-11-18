/*
 * camera_hw.c — implementação da camada de hardware das câmeras.
 *
 * DECISÃO DE PROJETO: UART por "polling" (consulta), não por interrupção.
 *   - O desafio diz que o modelo da UART no Renode suporta "bytes fragmentados
 *     nos dois sentidos", e NÃO promete suporte geral a transporte por IRQ de
 *     aplicação (docs/hardware-models.md). Polling é o caminho documentado.
 *   - uart_poll_in() nunca bloqueia: devolve -1 se não há byte. Então podemos
 *     chamá-la dentro do trabalho periódico do controle (a cada ~5 ms) sem
 *     travar a segurança da outra faixa.
 *   - uart_poll_out() espera só o registrador de transmissão esvaziar; para ~16
 *     bytes isso é rápido e determinístico.
 *
 * COMO ACHAMOS A UART?
 *   Pelo ALIAS do devicetree da placa (entry-camera / exit-camera), nunca por
 *   número de pino. Assim o código segue o contrato do desafio.
 */
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>

#include "camera_hw.h"

/* Índice 0 = entrada (USART3), índice 1 = saída (USART2), igual a enum parking_lane_id. */
static const struct device *const cameras[] = {
	DEVICE_DT_GET(DT_ALIAS(entry_camera)),
	DEVICE_DT_GET(DT_ALIAS(exit_camera)),
};

int camera_hw_init(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(cameras); ++i) {
		if (!device_is_ready(cameras[i])) {
			return -ENODEV;
		}
	}
	return 0;
}

int camera_hw_send(unsigned int lane, const uint8_t *data, size_t len)
{
	if (lane >= ARRAY_SIZE(cameras) || data == NULL) {
		return -EINVAL;
	}
	for (size_t i = 0U; i < len; ++i) {
		uart_poll_out(cameras[lane], data[i]);
	}
	return 0;
}

int camera_hw_read(unsigned int lane, uint8_t *buf, size_t max)
{
	size_t count = 0U;

	if (lane >= ARRAY_SIZE(cameras) || buf == NULL) {
		return -EINVAL;
	}
	while (count < max) {
		/*
		 * uart_poll_in(): 0 = leu um byte; -1 = nenhum byte disponível;
		 * qualquer outro negativo = erro do driver.
		 */
		int rc = uart_poll_in(cameras[lane], &buf[count]);

		if (rc == -1) {
			break; /* esvaziou o FIFO: terminamos por agora */
		}
		if (rc != 0) {
			return -EIO;
		}
		count++;
	}
	return (int)count;
}
