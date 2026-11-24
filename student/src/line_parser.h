/*
 * Montador de linhas limitado para as UARTs (REQ-11).
 *
 * Recebe um byte por vez e entrega uma linha só quando ela está completa e
 * bem formada. Regras:
 *  - quadro máximo de UART_FRAME_MAX bytes contando o terminador;
 *  - modo CRLF (câmera): exige CR imediatamente antes do LF;
 *    modo LF (pagamento): CR é proibido;
 *  - o corpo só tem ASCII imprimível (0x20..0x7E) e não é vazio;
 *  - uma linha começada há UART_ASSEMBLY_MS ou mais sem terminar é
 *    descartada junto com o resto até o próximo LF;
 *  - quadro longo demais ou malformado: descarta até o LF. Um prefixo
 *    válido nunca é entregue.
 * Cada UART tem a sua instância: os analisadores são independentes.
 */
#ifndef PARKING_LINE_PARSER_H
#define PARKING_LINE_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

enum lp_result {
	LP_NONE = 0, /* nada completo ainda */
	LP_LINE,     /* linha válida em lp_line() */
	LP_DROPPED,  /* um quadro foi descartado (diagnóstico/testes) */
};

struct line_parser {
	char buf[UART_FRAME_MAX + 1]; /* corpo + NUL */
	size_t len;                   /* bytes do quadro atual (sem o LF) */
	bool discarding;              /* jogando fora até o próximo LF */
	bool crlf;                    /* true: câmera (CRLF); false: P1 (LF) */
	int64_t start_ms;             /* chegada do primeiro byte do quadro */
};

/** Inicia vazio. crlf escolhe a regra do terminador. */
void lp_init(struct line_parser *p, bool crlf);

/** Processa um byte recebido no instante now_ms. */
enum lp_result lp_feed(struct line_parser *p, uint8_t byte, int64_t now_ms);

/** Linha entregue pelo último LP_LINE, sem terminador. */
const char *lp_line(const struct line_parser *p);

/**
 * Lê um decimal canônico (sem sinal, sem zero à esquerda, "0" só sozinho)
 * no início de *s, até max. Avança *s para depois dos dígitos.
 * @return true se válido.
 */
bool lp_parse_uint(const char **s, uint32_t max, uint32_t *out);

#endif /* PARKING_LINE_PARSER_H */
