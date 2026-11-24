/*
 * Montador de linhas de UART (REQ-11): lógica pura, sem hardware.
 *
 * Os bytes chegam em pedaços (até 8 bytes a cada 10 ms). O montador junta os
 * bytes até o LF e só entrega a linha se ela estiver inteira e bem formada:
 *
 *  - no máximo UART_FRAME_MAX (128) bytes contando o terminador;
 *  - só ASCII imprimível (0x20..0x7e) no corpo;
 *  - câmera: terminada em CR LF; terminal: terminada só em LF (CR proibido);
 *  - uma linha começada precisa terminar em UART_ASSEMBLY_MS (1000 ms).
 *
 * Qualquer violação descarta tudo até o próximo LF ("modo descarte"), para
 * nunca executar o prefixo de uma linha malformada. Há uma instância
 * independente por UART (câmera de entrada, câmera de saída, terminal).
 */
#ifndef LINE_PARSER_H_
#define LINE_PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parking.h"

/** Terminador exigido pelo protocolo daquela UART. */
enum line_ending {
	LINE_CRLF, /* câmera: AT, CR LF */
	LINE_LF,   /* terminal P1: só LF */
};

struct line_parser {
	enum line_ending ending;
	/** Corpo acumulado, sem terminador (cabe o maior corpo + '\0'). */
	char buf[UART_FRAME_MAX];
	/** Bytes recebidos na linha atual, incluindo CR, sem o LF. */
	size_t len;
	/** true: descartando até o próximo LF (linha inválida ou longa). */
	bool discarding;
	/** true: algum byte do corpo já era inválido (não imprimível, CR...). */
	bool bad;
	/** Instante do primeiro byte da linha atual (prazo de montagem). */
	int64_t first_ms;
};

/** Prepara um montador vazio para o terminador dado. */
void line_parser_init(struct line_parser *lp, enum line_ending ending);

/**
 * Entrega um byte recebido.
 *
 * @return comprimento da linha completa e válida (corpo, sem terminador,
 *         terminado em '\0' em lp->buf) ou -1 se ainda não há linha válida.
 */
int line_parser_feed(struct line_parser *lp, uint8_t byte, int64_t now_ms);

/**
 * Aplica o prazo de montagem: uma linha parcial com mais de
 * UART_ASSEMBLY_MS é descartada até o próximo LF.
 */
void line_parser_poll(struct line_parser *lp, int64_t now_ms);

#endif /* LINE_PARSER_H_ */
