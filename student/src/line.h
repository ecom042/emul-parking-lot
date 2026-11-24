/**
 * @file line.h
 * @brief Montagem limitada de quadros de texto recebidos por UART.
 *
 * Cada analisador (câmera e pagamento) tem a sua instância. Um quadro tem no
 * máximo LINE_MAX_FRAME bytes contando o terminador, só ASCII imprimível e
 * precisa terminar em até LINE_ASSEMBLY_MS desde o primeiro byte. O que fugir
 * disso é descartado até o próximo LF, e nenhum prefixo é entregue.
 */

#ifndef LINE_H_
#define LINE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Tamanho máximo de um quadro, terminador incluído. */
#define LINE_MAX_FRAME 128

/** @brief Prazo para montar um quadro a partir do primeiro byte, em ms. */
#define LINE_ASSEMBLY_MS 1000

/** @brief Resultado de entregar um byte ao montador. */
enum line_result {
	LINE_NONE = 0, /**< Byte guardado; o quadro ainda não terminou. */
	LINE_READY,    /**< Quadro completo em line_frame(). */
	LINE_DROPPED,  /**< Quadro inválido descartado neste LF. */
};

/** @brief Estado de um montador de quadros. */
struct line_framer {
	char buf[LINE_MAX_FRAME]; /**< Corpo do quadro, sem terminador. */
	size_t len;               /**< Bytes válidos em buf. */
	bool crlf;                /**< true exige CR LF; false exige só LF. */
	bool discarding;          /**< Descartando até o próximo LF. */
	bool started;             /**< Há um quadro parcial em montagem. */
	int64_t first_byte_ms;    /**< Instante do primeiro byte do quadro. */
};

/**
 * @brief Prepara um montador vazio.
 *
 * @param f Montador.
 * @param crlf true para quadros terminados em CR LF (câmera), false para LF
 *             (pagamento, que rejeita CR).
 */
void line_init(struct line_framer *f, bool crlf);

/**
 * @brief Entrega um byte recebido.
 *
 * @param f Montador.
 * @param byte Byte recebido.
 * @param now_ms Tempo virtual atual.
 * @return Resultado da entrega.
 */
enum line_result line_feed(struct line_framer *f, uint8_t byte, int64_t now_ms);

/**
 * @brief Descarta um quadro parcial cujo prazo de montagem venceu.
 *
 * Depois disso, os bytes seguintes são ignorados até o próximo LF.
 *
 * @param f Montador.
 * @param now_ms Tempo virtual atual.
 * @return true se um quadro parcial foi descartado.
 */
bool line_expire(struct line_framer *f, int64_t now_ms);

/**
 * @brief Corpo do último quadro completo, sem terminador e terminado em NUL.
 *
 * Válido até o próximo line_feed().
 *
 * @param f Montador.
 * @return Texto do quadro.
 */
const char *line_frame(const struct line_framer *f);

#endif /* LINE_H_ */
