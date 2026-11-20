/*
 * camera_parser.h — Analisador (parser) das linhas que a câmera envia pela UART
 * ==========================================================================
 *
 * PARA QUE SERVE ESTE ARQUIVO?
 *   A câmera de placas fala um protocolo de texto (comandos "AT"). Ela
 *   responde ao firmware com linhas como:
 *
 *       OK\r\n
 *       ERROR\r\n
 *       +PLATE:17,"ABC1D23",93\r\n      (id da captura, placa, confiança)
 *
 *   O problema: a UART entrega BYTE A BYTE, e a câmera simulada ainda
 *   "fragmenta" a resposta em pedaços de até 8 bytes, um a cada 10 ms. Então
 *   o firmware nunca recebe "a linha pronta": ele precisa MONTAR a linha.
 *   É exatamente isso que este módulo faz, e só isso.
 *
 * POR QUE UM MÓDULO À PARTE (e sem nenhum include do Zephyr)?
 *   Regra de ouro do desafio: separar a LÓGICA da camada que toca o hardware.
 *   Este módulo é lógica pura: recebe bytes, devolve "quadros" (frames).
 *   Como não depende de driver nenhum, dá para testá-lo com ztest sem falsos
 *   (FFF) e cobrir todos os desvios (branches) — isso vale pontos de
 *   cobertura (REQ-11).
 *
 * O QUE ESTE PARSER GARANTE (REQ-11):
 *   1. Quadro de no máximo 128 bytes, contando o terminador CR LF.
 *   2. Montagem parcial com prazo: se a linha não terminar em 1000 ms contados
 *      do PRIMEIRO byte, o que já chegou é descartado.
 *   3. Se a linha for inválida (caractere proibido, grande demais, sem CR,
 *      campo mal formado), descarta-se TUDO ATÉ O PRÓXIMO LF e volta a
 *      sincronizar. Nunca se "aproveita" um prefixo que pareça válido.
 *   4. Validação rígida: id canônico (1..999999999, sem zero à esquerda),
 *      placa [A-Z]{3}[0-9][A-Z0-9][0-9]{2}, confiança canônica 0..100.
 *
 * O QUE ESTE PARSER NÃO FAZ:
 *   Ele não decide se a placa é aceita (confiança >= 85, id atual...). Isso é
 *   política de captura e fica em camera.c. O parser só diz "chegou um quadro
 *   X com estes campos".
 */
#ifndef PARKING_CAMERA_PARSER_H
#define PARKING_CAMERA_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tamanho máximo de um quadro, INCLUINDO o terminador CR LF (contrato do desafio). */
#define CAMERA_FRAME_MAX 128U

/* Prazo para uma linha parcial terminar, contado do primeiro byte (virtual, em ms). */
#define CAMERA_FRAME_TIMEOUT_MS 1000

/* Uma placa tem 7 caracteres (ex.: ABC1D23). O buffer guarda mais o '\0'. */
#define CAMERA_PLATE_LEN 7U

/* Limites do protocolo. */
#define CAMERA_ID_MAX 999999999U
#define CAMERA_CONFIDENCE_MAX 100U

/* Que tipo de quadro acabou de ser concluído? */
enum camera_frame_kind {
	CAMERA_FRAME_NONE,    /* ainda não há quadro completo (faltam bytes) */
	CAMERA_FRAME_OK,      /* "OK": a câmera ACEITOU o comando (não é a placa!) */
	CAMERA_FRAME_ERROR,   /* "ERROR": a câmera rejeitou o comando */
	CAMERA_FRAME_PLATE,   /* "+PLATE:id,"placa",conf": o resultado da captura */
	CAMERA_FRAME_INVALID, /* uma linha chegou ao LF, mas estava malformada: foi descartada */
};

/* Conteúdo de um quadro. Os campos id/plate/confidence só valem se kind == PLATE. */
struct camera_frame {
	enum camera_frame_kind kind;
	uint32_t id;                         /* id da captura que a câmera está respondendo */
	char plate[CAMERA_PLATE_LEN + 1U];   /* placa terminada em '\0' */
	uint8_t confidence;                  /* 0..100 */
};

/*
 * Estado de UM analisador. Cada câmera (entrada e saída) tem o seu próprio:
 * "analisadores independentes" é exigência do REQ-11 — o lixo de uma UART
 * jamais pode contaminar a outra.
 */
struct camera_parser {
	char buf[CAMERA_FRAME_MAX]; /* bytes da linha em montagem (guarda até 127) */
	size_t len;                 /* quantos bytes já estão em buf */
	bool discarding;            /* true: descartando até o próximo LF */
	int64_t started_at;         /* instante do 1º byte da linha em montagem */
};

/* Coloca o analisador no estado inicial (vazio, sincronizado). */
void camera_parser_init(struct camera_parser *parser);

/*
 * Entrega UM byte recebido da UART no instante `now` (ms, tempo virtual).
 * Devolve o tipo de quadro que esse byte CONCLUIU:
 *   - CAMERA_FRAME_NONE    -> o byte só entrou no buffer (ou foi descartado);
 *   - OK/ERROR/PLATE       -> quadro válido pronto em *frame;
 *   - CAMERA_FRAME_INVALID -> uma linha ruim terminou e foi jogada fora.
 * *frame é sempre escrito (pelo menos o campo kind).
 */
enum camera_frame_kind camera_parser_feed(struct camera_parser *parser, uint8_t byte,
					  int64_t now, struct camera_frame *frame);

/*
 * Aplica o prazo de 1000 ms a uma linha parcial. camera_parser_feed() já chama
 * isto sozinha; é exposta porque quem chama pode querer expirar antes de ler.
 */
void camera_parser_expire(struct camera_parser *parser, int64_t now);

#endif /* PARKING_CAMERA_PARSER_H */
