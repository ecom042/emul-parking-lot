/*
 * payment.h — Cobrança na saída (REQ-05, REQ-11 no terminal, REQ-12 tarifa)
 * =========================================================================
 *
 * PARA QUE SERVE?
 *   Implementa a METADE "pagamento" de `struct parking_equipment` (parking.h):
 *
 *       payment_start  -> payment_start()
 *       payment_poll   -> payment_poll()
 *       payment_cancel -> payment_cancel()
 *
 *   O controle (parking.c) só diz "cobrar esta placa, que entrou em T" e
 *   pergunta "já pagou?". Tudo o que é tarifa, display, UUID, retransmissão e
 *   conferência do PAID mora aqui.
 *
 * A SEQUÊNCIA (por que nessa ordem — exigência do desafio e de INTERFACES.md):
 *
 *   start ─► [1] desenha a PLACA no display ─► [2] CONGELA a tarifa
 *         ─► [3] desenha a COBRANÇA no display ─► [4] envia P1 CHARGE ao terminal
 *         ─► [5] espera P1 RESULT ... PAID (retransmite se calar)
 *
 *   - O valor é calculado UMA vez, no passo [2], e nunca muda: nem o display
 *     nem as retransmissões o recalculam, mesmo que o tempo passe.
 *   - A cobrança só é enviada depois do display terminar (o terminal "mostra" o
 *     que o cliente vai pagar).
 *   - O prazo de 1000 ms do terminal só começa ao ENVIAR a cobrança; o tempo de
 *     desenho do display não o consome.
 *
 * REGRAS DE SEGURANÇA (REQ-05): a cancela só abre (EQUIPMENT_OK) com um
 * `P1 RESULT <uuid ATUAL> PAID <valor CONGELADO> BRL`. Resposta ausente, de UUID
 * antigo, com outro valor, CANCELLED ou malformada NUNCA abre. São no máximo 3
 * envios (mesmo UUID e mesmo valor), 1000 ms entre eles. Esgotado: o firmware
 * invalida o UUID e manda `P1 CANCEL <uuid>`; o resultado é EQUIPMENT_FAILED
 * (o controle pede assistência).
 *
 * COMO O DISPLAY ENTRA (injeção de dependência):
 *   O display (SPI) é a issue #7. Para este módulo não depender dele em tempo de
 *   link, ele recebe em payment_init() uma tabela de funções (`struct
 *   payment_display_ops`). O display real (display.c) oferecerá essas funções, e
 *   na integração (#8) basta passar a tabela. Nos testes passam-se falsos FFF.
 *   Os desenhos são NÃO BLOQUEANTES: draw_*() só pede; poll() diz se terminou.
 *   Assim, empurrar pixels pela SPI nunca atrasa a segurança da outra faixa.
 *
 * TEMPO: `now` é sempre o uptime virtual (k_uptime_get()), passado pelo chamador.
 */
#ifndef PARKING_PAYMENT_H
#define PARKING_PAYMENT_H

#include <stdint.h>

#include "parking.h"

#define PAYMENT_ATTEMPTS 3             /* envios da mesma cobrança */
#define PAYMENT_ATTEMPT_TIMEOUT_MS 1000 /* espera por envio */
#define PAYMENT_READ_CHUNK 16U
#define PAYMENT_READ_ROUNDS 8

/* Contrato que o módulo do display (issue #7) precisa cumprir. */
struct payment_display_ops {
	/* Pede para desenhar a tela com a PLACA reconhecida. 0 = pedido aceito; < 0 = falha. */
	int (*draw_plate)(const char plate[PARKING_PLATE_SIZE]);
	/* Pede para desenhar a COBRANÇA (placa, BRL r.cc, SIMULATED PAYMENT, NOT PIX). */
	int (*draw_charge)(const char plate[PARKING_PLATE_SIZE], uint32_t amount_centavos);
	/* Andamento do último desenho pedido: PENDING, OK (terminou) ou FAILED. Não bloqueia. */
	enum equipment_result (*poll)(int64_t now);
	/* Abandona o desenho em andamento. */
	void (*cancel)(void);
};

/*
 * Registra o display, zera o estado e confere a UART. 0 ou -errno (-EINVAL se a tabela for
 * incompleta, -ENODEV se a UART não estiver pronta). Se falhar, o módulo fica não inicializado.
 */
int payment_init(const struct payment_display_ops *display);

/* Esquece cobrança em andamento, UUID, valor e bytes parciais (REQ-13). Não envia nada. */
void payment_reset(void);

/*
 * Pede uma cobrança. `entered_at` = instante (ms virtuais) da passagem na entrada.
 * Só REGISTRA o pedido; o trabalho acontece em payment_poll().
 * -ENODEV sem payment_init(); -EBUSY se já há cobrança; -EINVAL placa inválida.
 */
int payment_start(const char plate[PARKING_PLATE_SIZE], int64_t entered_at);

/* Avança a cobrança. PENDING = ainda trabalhando; OK = PAID confirmado; FAILED = assistência. */
enum equipment_result payment_poll(int64_t now);

/* Abandona a cobrança: invalida o UUID e, se já enviou a cobrança, manda P1 CANCEL. */
void payment_cancel(void);

#endif /* PARKING_PAYMENT_H */
