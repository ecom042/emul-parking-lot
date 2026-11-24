/**
 * @file payment.h
 * @brief Cobrança no terminal de pagamento pelo protocolo P1 (LPUART1).
 *
 * Uma tentativa de saída tem um UUID novo e um valor congelado. A cobrança é
 * enviada no máximo PAYMENT_MAX_SENDS vezes, com o mesmo UUID e valor, a cada
 * PAYMENT_TIMEOUT_MS sem resposta. Só um PAID com o UUID atual, o mesmo valor e
 * BRL confirma o pagamento. O controle da faixa chama payment_poll() sob
 * o mutex de atendimento. A leitura da UART é feita com hw_uart_read().
 */

#ifndef PAYMENT_H_
#define PAYMENT_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Máximo de envios da mesma cobrança, o primeiro incluído. */
#define PAYMENT_MAX_SENDS 3

/** @brief Prazo de resposta de cada envio, em ms de tempo virtual. */
#define PAYMENT_TIMEOUT_MS 1000

/** @brief Tamanho de um UUID textual (8-4-4-4-12), sem o NUL. */
#define PAYMENT_UUID_LEN 36

/** @brief Tamanho da placa, sem o NUL. */
#define PAYMENT_PLATE_LEN 7

/** @brief Maior valor aceito pelo terminal, em centavos. */
#define PAYMENT_MAX_AMOUNT 1000000U

/** @brief Desfecho de uma cobrança, devolvido por payment_poll(). */
enum payment_event {
	PAYMENT_EV_NONE = 0, /**< Nada novo: aguardando ou sem cobrança ativa. */
	PAYMENT_EV_PAID,     /**< PAID exato da cobrança atual: pode abrir. */
	PAYMENT_EV_FAILED,   /**< CANCELLED ou envios esgotados: assistência. */
};

/**
 * @brief Prepara o módulo, sem cobrança ativa.
 *
 * @return 0 em sucesso ou errno negativo.
 */
int payment_init(void);

/**
 * @brief Inicia uma cobrança nova e faz o primeiro envio do CHARGE.
 *
 * Gera um UUID novo, distinto de todos os anteriores desde o boot.
 *
 * @param plate Placa reconhecida, já validada.
 * @param amount Valor congelado, em centavos (1..1000000).
 * @param now_ms Tempo virtual atual.
 * @return 0 em sucesso, -EINVAL para argumentos inválidos, -EBUSY se já há
 *         cobrança ativa ou o erro do envio pela UART.
 */
int payment_start(const char *plate, uint32_t amount, int64_t now_ms);

/**
 * @brief Processa os bytes recebidos e os prazos da cobrança ativa.
 *
 * Deve ser chamada periodicamente. Respostas malformadas, de outro UUID, com
 * outro valor ou sem cobrança ativa são descartadas sem efeito.
 *
 * @param now_ms Tempo virtual atual.
 * @param authorize true se a distância atual permite usar um PAID. Enquanto
 *        false, o PAID válido fica retido e a cobrança ainda pode ser cancelada.
 * @return Desfecho novo da cobrança, se houver. Depois de PAID ou FAILED a
 *         cobrança deixa de estar ativa.
 */
enum payment_event payment_poll(int64_t now_ms, bool authorize);

/**
 * @brief Desiste da cobrança ativa (carro saiu, assistência, reset).
 *
 * Invalida o UUID antes de enviar P1 CANCEL, para que nenhuma resposta
 * atrasada autorize algo. Sem cobrança ativa, não faz nada.
 *
 * @return 0 em sucesso ou o erro do envio pela UART.
 */
int payment_abort(void);

/**
 * @brief Informa se há cobrança ativa.
 *
 * @return 1 se há, 0 se não.
 */
int payment_active(void);

/** @brief Contadores de diagnóstico, desde payment_init(). */
struct payment_stats {
	unsigned int dropped;         /**< Quadros descartados no LF (longos, com CR, inválidos). */
	unsigned int expired;         /**< Quadros parciais que passaram do prazo de montagem. */
	unsigned int cancel_failures; /**< CANCEL que a UART não conseguiu enviar. */
};

/**
 * @brief Copia os contadores de diagnóstico.
 *
 * Só observação: nenhum contador muda o andamento da cobrança.
 *
 * @param stats Recebe os contadores.
 */
void payment_stats_get(struct payment_stats *stats);

#endif /* PAYMENT_H_ */
