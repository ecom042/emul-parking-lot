/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Bordas dos parsers que a suite original nao alcanca: cada condicao das
 * validacoes da camera e do terminal, nos dois sentidos, mais os caminhos de
 * montagem (prazo exato, linha de 128 bytes, reset) e os argumentos nulos.
 * Cada linha rejeitada tambem confere que o quadro de saida ficou intacto e
 * que o analisador voltou ao estado de linha vazia.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "at_parser.h"
#include "p1_parser.h"

#define UUID_BOM	"3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1"

/* Alimenta a linha inteira no mesmo instante; devolve o ultimo retorno. */
static int at_linha(struct at_parser *p, const char *s, int64_t now,
		    struct at_plate *out)
{
	int rc = 0;

	for (const char *q = s; *q != '\0'; q++) {
		rc = at_parser_feed(p, *q, now, out);
		zassert_true(rc >= 0, "byte valido nunca e erro de argumento");
		if (q[1] != '\0') {
			zassert_equal(rc, 0, "so o LF fecha um quadro");
		}
	}
	return rc;
}

static int p1_linha(struct p1_parser *p, const char *s, int64_t now,
		    struct p1_result *out)
{
	int rc = 0;

	for (const char *q = s; *q != '\0'; q++) {
		rc = p1_parser_feed(p, *q, now, out);
		zassert_true(rc >= 0, "byte valido nunca e erro de argumento");
		if (q[1] != '\0') {
			zassert_equal(rc, 0, "so o LF fecha um quadro");
		}
	}
	return rc;
}

/* Rejeita a linha sem tocar no quadro de saida e deixa o parser limpo. */
static void at_rejeita(const char *s)
{
	struct at_parser p = {0};
	struct at_plate out;
	struct at_plate antes;

	memset(&out, 0x5a, sizeof(out));
	memcpy(&antes, &out, sizeof(out));
	zassert_equal(at_linha(&p, s, 0, &out), 0, "aceitou: %s", s);
	zassert_mem_equal(&out, &antes, sizeof(out), "escreveu a saida: %s", s);
	zassert_equal(p.len, 0);
	zassert_false(p.discarding);
	zassert_false(p.have_first);
}

static void p1_rejeita(const char *s)
{
	struct p1_parser p = {0};
	struct p1_result out;
	struct p1_result antes;

	memset(&out, 0x5a, sizeof(out));
	memcpy(&antes, &out, sizeof(out));
	zassert_equal(p1_linha(&p, s, 0, &out), 0, "aceitou: %s", s);
	zassert_mem_equal(&out, &antes, sizeof(out), "escreveu a saida: %s", s);
	zassert_equal(p.len, 0);
	zassert_false(p.discarding);
	zassert_false(p.have_first);
}

/* Monta +PLATE:17,"<placa>",93 CR LF com a placa dada. */
static void at_rejeita_placa(const char *placa)
{
	char linha[48];

	snprintf(linha, sizeof(linha), "+PLATE:17,\"%s\",93\r\n", placa);
	at_rejeita(linha);
}

/* --- camera --- */

ZTEST(parsers, test_at_id_com_caractere_nao_decimal)
{
	at_rejeita("+PLATE:-1,\"ABC1D23\",93\r\n");
	at_rejeita("+PLATE:A1,\"ABC1D23\",93\r\n");
	at_rejeita("+PLATE:0A,\"ABC1D23\",93\r\n");
	at_rejeita("+PLATE:12A,\"ABC1D23\",93\r\n");
	at_rejeita("+PLATE:1:,\"ABC1D23\",93\r\n");
}

ZTEST(parsers, test_at_id_estoura_antes_do_fim)
{
	/* Nove digitos ja passam de 100000000: o decimo nem e somado. */
	at_rejeita("+PLATE:1000000010,\"ABC1D23\",93\r\n");
	at_rejeita("+PLATE:99999999999,\"ABC1D23\",93\r\n");
}

ZTEST(parsers, test_at_confianca_nas_bordas)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	zassert_equal(at_linha(&p, "+PLATE:5,\"ABC1D23\",0\r\n", 0, &out), 1,
		      "confianca zero e decimal canonico");
	zassert_equal(out.id, 5);
	zassert_equal(out.confidence, 0);

	zassert_equal(at_linha(&p, "+PLATE:6,\"ABC1D23\",100\r\n", 0, &out), 1);
	zassert_equal(out.confidence, 100);

	at_rejeita("+PLATE:7,\"ABC1D23\",-5\r\n");
	at_rejeita("+PLATE:7,\"ABC1D23\",A\r\n");
	at_rejeita("+PLATE:7,\"ABC1D23\",9X\r\n");
	at_rejeita("+PLATE:7,\"ABC1D23\",00\r\n");
	at_rejeita("+PLATE:7,\"ABC1D23\",93 \r\n");
}

ZTEST(parsers, test_at_menor_quadro_valido)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	/*
	 * ID e confianca de um digito: 20 bytes antes do CR LF. Os IDs da faixa
	 * comecam em 1, entao a primeira captura com confianca baixa tem esse
	 * tamanho e precisa chegar a FSM para disparar a nova tentativa na hora.
	 */
	zassert_equal(at_linha(&p, "+PLATE:1,\"ABC1D23\",9\r\n", 0, &out), 1);
	zassert_equal(out.id, 1);
	zassert_str_equal(out.plate, "ABC1D23");
	zassert_equal(out.confidence, 9);

	/* Um byte a menos (placa de 6) ja e curto demais. */
	at_rejeita("+PLATE:1,\"ABC1D2\",9\r\n");
}

ZTEST(parsers, test_at_placa_letras_iniciais)
{
	/* '@' fica logo abaixo de 'A' e '[' logo acima de 'Z'. */
	at_rejeita_placa("@BC1D23");
	at_rejeita_placa("[BC1D23");
	at_rejeita_placa("A@C1D23");
	at_rejeita_placa("A[C1D23");
	at_rejeita_placa("AB@1D23");
	at_rejeita_placa("AB[1D23");
}

ZTEST(parsers, test_at_placa_digito_e_alfanumerico)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	/* '/' fica logo abaixo de '0' e ':' logo acima de '9'. */
	at_rejeita_placa("ABC/D23");
	at_rejeita_placa("ABC:D23");
	at_rejeita_placa("ABCAD23");
	at_rejeita_placa("ABC1/23");
	at_rejeita_placa("ABC1:23");
	at_rejeita_placa("ABC1@23");
	at_rejeita_placa("ABC1[23");
	at_rejeita_placa("ABC1d23");

	/* As duas formas validas da quinta posicao. */
	zassert_equal(at_linha(&p, "+PLATE:8,\"ABC1Z99\",90\r\n", 0, &out), 1);
	zassert_str_equal(out.plate, "ABC1Z99");
	zassert_equal(at_linha(&p, "+PLATE:9,\"ZZZ9000\",90\r\n", 0, &out), 1);
	zassert_str_equal(out.plate, "ZZZ9000");
}

ZTEST(parsers, test_at_placa_digitos_finais)
{
	at_rejeita_placa("ABC1D/3");
	at_rejeita_placa("ABC1D:3");
	at_rejeita_placa("ABC1D2/");
	at_rejeita_placa("ABC1D2:");
	at_rejeita_placa("ABC1DA3");
	at_rejeita_placa("ABC1D2A");
}

ZTEST(parsers, test_at_separadores_e_tamanho)
{
	/* Linha curta demais para ser +PLATE, mesmo com prefixo certo. */
	at_rejeita("+PLATE:1,\"A\",9\r\n");
	/* Tamanho de placa, prefixo errado. */
	at_rejeita("-PLATE:17,\"ABC1D23\",93\r\n");
	at_rejeita("+plate:17,\"ABC1D23\",93\r\n");
	/* Separadores depois do ID e depois da placa. */
	at_rejeita("+PLATE:17;\"ABC1D23\",93\r\n");
	at_rejeita("+PLATE:17 \"ABC1D23\",93\r\n");
	at_rejeita("+PLATE:17,'ABC1D23',93\r\n");
	at_rejeita("+PLATE:17,\"ABC1D23',93\r\n");
	at_rejeita("+PLATE:17,\"ABC1D23\";93\r\n");
	/* Placa curta: nao cabem placa, aspas, virgula e confianca. */
	at_rejeita("+PLATE:123456789,\"ABC1\",9\r\n");
}

ZTEST(parsers, test_at_linhas_curtas)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	at_rejeita("\n");
	at_rejeita("\r\n");
	at_rejeita("NO\r\n");
	at_rejeita("ERRAR\r\n");
	at_rejeita("OKAY\r\n");
	at_rejeita("ERRORS\r\n");

	/* Depois de tudo isso o mesmo parser ainda aceita uma placa. */
	zassert_equal(at_linha(&p, "\n", 0, &out), 0);
	zassert_equal(at_linha(&p, "NO\r\n", 0, &out), 0);
	zassert_equal(at_linha(&p, "+PLATE:40,\"ABC1D23\",93\r\n", 0, &out), 1);
	zassert_equal(out.id, 40);
}

ZTEST(parsers, test_at_linha_de_128_bytes_e_lf)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	/* 128 bytes enchem o buffer; o LF chega quando ja nao ha espaco. */
	for (int i = 0; i < FRAME_MAX; i++) {
		zassert_equal(at_parser_feed(&p, 'A', 0, &out), 0);
	}
	zassert_equal(p.len, FRAME_MAX);
	zassert_equal(at_parser_feed(&p, '\n', 0, &out), 0);
	zassert_equal(p.len, 0);
	zassert_false(p.discarding, "o LF que estoura ja fecha o descarte");
	zassert_false(p.have_first);

	zassert_equal(at_linha(&p, "+PLATE:41,\"ABC1D23\",93\r\n", 0, &out), 1);
	zassert_equal(out.id, 41);
}

ZTEST(parsers, test_at_estouro_descarta_prefixo_valido)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	for (int i = 0; i < FRAME_MAX; i++) {
		zassert_equal(at_parser_feed(&p, 'A', 0, &out), 0);
	}
	/* O quadro valido que chega colado ao estouro e o sufixo da linha. */
	zassert_equal(at_linha(&p, "+PLATE:42,\"ABC1D23\",93\r\n", 0, &out), 0);
	zassert_true(p.len == 0 && !p.discarding);
	zassert_equal(at_linha(&p, "+PLATE:43,\"ABC1D23\",93\r\n", 0, &out), 1);
	zassert_equal(out.id, 43);
}

ZTEST(parsers, test_at_prazo_exato_ainda_vale)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	zassert_equal(at_linha(&p, "+PLATE:44,\"ABC", 5000, &out), 0);
	zassert_equal(p.first_ms, 5000);
	zassert_equal(at_linha(&p, "1D23\",93\r\n", 5000 + FRAME_MS, &out), 1,
		      "1000 ms exatos ainda estao dentro do prazo");
	zassert_equal(out.id, 44);
}

ZTEST(parsers, test_at_prazo_vencido_descarta_ate_lf)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	zassert_equal(at_linha(&p, "+PLATE:45", 0, &out), 0);
	zassert_equal(at_parser_feed(&p, ',', FRAME_MS + 1, &out), 0);
	zassert_true(p.discarding);
	zassert_equal(p.len, 0, "a linha vencida e jogada fora");
	zassert_equal(at_linha(&p, "\"ABC1D23\",93\r\n", FRAME_MS + 2, &out), 0);
	zassert_false(p.discarding);
	/* O prazo da linha nova conta a partir do primeiro byte dela. */
	zassert_equal(at_linha(&p, "+PLATE:46,\"ABC1D23\",93\r\n", 9000, &out), 1);
	zassert_equal(out.id, 46);
}

ZTEST(parsers, test_at_reset)
{
	struct at_parser p = {0};
	struct at_plate out = {0};

	zassert_equal(at_linha(&p, "+PLATE:47,\"AB", 100, &out), 0);
	zassert_true(p.len > 0);
	at_parser_reset(&p);
	zassert_equal(p.len, 0);
	zassert_false(p.have_first);
	zassert_false(p.discarding);
	/* Sem o reset, o resto da linha antiga viraria um quadro invalido. */
	zassert_equal(at_linha(&p, "+PLATE:48,\"ABC1D23\",93\r\n", 100, &out), 1);
	zassert_equal(out.id, 48);

	at_parser_reset(NULL);
}

ZTEST(parsers, test_at_saida_nula_nao_consome)
{
	struct at_parser p = {0};

	zassert_equal(at_parser_feed(&p, '+', 0, NULL), -EINVAL);
	zassert_equal(p.len, 0, "argumento nulo nao consome o byte");
	zassert_false(p.have_first);
}

ZTEST(parsers, test_at_descarte_sem_marca_de_tempo)
{
	/*
	 * Estado montado a mao (a API nunca o produz): descarte pendente sem
	 * marca de tempo. O parser continua descartando ate o LF, nao marca o
	 * primeiro byte e nao aplica prazo.
	 */
	struct at_parser p = {0};
	struct at_plate out = {0};

	p.discarding = true;
	zassert_equal(at_linha(&p, "+PLATE:49,\"ABC1D23\",93\r\n", 7000, &out), 0);
	zassert_false(p.discarding);
	zassert_false(p.have_first);
	zassert_equal(at_linha(&p, "+PLATE:50,\"ABC1D23\",93\r\n", 7000, &out), 1);
	zassert_equal(out.id, 50);
}

/* --- terminal --- */

ZTEST(parsers, test_p1_valor_nas_bordas)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};

	zassert_equal(p1_linha(&p, "P1 RESULT " UUID_BOM " PAID 5 BRL\n", 0, &out), 1,
		      "um digito so e canonico");
	zassert_equal(out.cents, 5);
	zassert_equal(out.error, P1_ERROR_NONE);

	zassert_equal(p1_linha(&p, "P1 RESULT " UUID_BOM " PAID 1000000 BRL\n", 0, &out),
		      1);
	zassert_equal(out.cents, 1000000);

	p1_rejeita("P1 RESULT " UUID_BOM " PAID 0 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " PAID 1000001 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " PAID 4a0 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " PAID 4-0 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " PAID 4:0 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " PAID 1000000010 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " PAID 99999999999 BRL\n");
}

ZTEST(parsers, test_p1_uuid_invalido)
{
	/* Tamanho errado. */
	p1_rejeita("P1 RESULT 3f2a9c10-7b4e PAID 400 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM "0 PAID 400 BRL\n");
	/* Caracteres fora de [0-9a-f] e hifen no lugar de um digito. */
	p1_rejeita("P1 RESULT 3f2a9c1g-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n");
	p1_rejeita("P1 RESULT 3f2a9c1/-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n");
	p1_rejeita("P1 RESULT 3f2a9c1:-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n");
	p1_rejeita("P1 RESULT 3f2a9c1`-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n");
	p1_rejeita("P1 RESULT 3f2a-c10-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n");
	/* Hifen faltando em cada um dos quatro lugares. */
	p1_rejeita("P1 RESULT 3f2a9c100-7b4-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n");
	p1_rejeita("P1 RESULT 3f2a9c10-7b4e04d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n");
	p1_rejeita("P1 RESULT 3f2a9c10-7b4e-4d2109a6f-0c5e8b7d12c1 PAID 400 BRL\n");
	p1_rejeita("P1 RESULT 3f2a9c10-7b4e-4d21-9a6f00c5e8b7d12c1 PAID 400 BRL\n");
}

ZTEST(parsers, test_p1_campos_errados)
{
	/* Sete campos: um a mais que o maior quadro. */
	p1_rejeita("P1 RESULT " UUID_BOM " PAID 400 BRL X\n");
	/* Cabecalho, verbo, status e moeda errados. */
	p1_rejeita("P2 RESULT " UUID_BOM " PAID 400 BRL\n");
	p1_rejeita("P2 ERROR FRAME\n");
	p1_rejeita("P1 RESULTS " UUID_BOM " PAID 400 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " PENDING 400 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " paid 400 BRL\n");
	p1_rejeita("P1 RESULT " UUID_BOM " PAID 400 USD\n");
	p1_rejeita("P1 STATUS FRAME\n");
	/* Numero de campos que nao e nem RESULT nem ERROR. */
	p1_rejeita("P1 ERROR FRAME X\n");
	p1_rejeita("P1 ERROR\n");
	p1_rejeita("P1\n");
	/* Campo vazio no comeco, no fim e linha vazia. */
	p1_rejeita(" P1 ERROR FRAME\n");
	p1_rejeita("P1 ERROR FRAME \n");
	p1_rejeita("\n");
	/* CR em qualquer lugar da linha, nao so antes do LF. */
	p1_rejeita("P1 ERROR\rFRAME\n");
}

ZTEST(parsers, test_p1_error_cada_codigo)
{
	static const struct {
		const char *linha;
		enum p1_error error;
	} casos[] = {
		{ "P1 ERROR FRAME\n", P1_ERROR_FRAME },
		{ "P1 ERROR TIMEOUT\n", P1_ERROR_TIMEOUT },
		{ "P1 ERROR CAPACITY\n", P1_ERROR_CAPACITY },
		{ "P1 ERROR CONFLICT\n", P1_ERROR_CONFLICT },
		{ "P1 ERROR UNKNOWN\n", P1_ERROR_UNKNOWN },
	};
	struct p1_parser p = {0};
	struct p1_result out = {0};

	for (size_t i = 0; i < ARRAY_SIZE(casos); i++) {
		/* Um RESULT antes prova que o ERROR limpa UUID e valor. */
		zassert_equal(p1_linha(&p, "P1 RESULT " UUID_BOM " PAID 400 BRL\n", 0,
				       &out), 1);
		zassert_equal(p1_linha(&p, casos[i].linha, 0, &out), 1, "%s",
			      casos[i].linha);
		zassert_equal(out.status, P1_STATUS_ERROR);
		zassert_equal(out.error, casos[i].error, "%s", casos[i].linha);
		zassert_equal(out.uuid[0], '\0');
		zassert_equal(out.cents, 0);
	}

	p1_rejeita("P1 ERROR TIMEOUTS\n");
	p1_rejeita("P1 ERROR conflict\n");
	p1_rejeita("P1 ERROR NONE\n");
}

ZTEST(parsers, test_p1_cancelled_preenche_tudo)
{
	struct p1_parser p = {0};
	struct p1_result out;

	memset(&out, 0x5a, sizeof(out));
	zassert_equal(p1_linha(&p, "P1 RESULT " UUID_BOM " CANCELLED 250 BRL\n", 0, &out),
		      1);
	zassert_equal(out.status, P1_STATUS_CANCELLED);
	zassert_equal(out.error, P1_ERROR_NONE);
	zassert_equal(out.cents, 250);
	zassert_str_equal(out.uuid, UUID_BOM);
}

ZTEST(parsers, test_p1_prazo_exato_ainda_vale)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};

	zassert_equal(p1_linha(&p, "P1 ERROR ", 3000, &out), 0);
	zassert_equal(p.first_ms, 3000);
	zassert_equal(p1_linha(&p, "TIMEOUT\n", 3000 + FRAME_MS, &out), 1);
	zassert_equal(out.error, P1_ERROR_TIMEOUT);
}

ZTEST(parsers, test_p1_prazo_vencido_descarta_ate_lf)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};

	zassert_equal(p1_linha(&p, "P1 ERROR ", 0, &out), 0);
	zassert_equal(p1_parser_feed(&p, 'F', FRAME_MS + 1, &out), 0);
	zassert_true(p.discarding);
	zassert_equal(p.len, 0, "a linha vencida e jogada fora");
	/* O sufixo forma um P1 ERROR sozinho, mas faz parte da linha vencida. */
	zassert_equal(p1_linha(&p, "P1 ERROR FRAME\n", FRAME_MS + 2, &out), 0);
	zassert_false(p.discarding);
	zassert_false(p.have_first);
	zassert_equal(p1_linha(&p, "P1 ERROR CONFLICT\n", 8000, &out), 1);
	zassert_equal(out.error, P1_ERROR_CONFLICT);
}

ZTEST(parsers, test_p1_linha_longa_descarta_ate_lf)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};

	for (int i = 0; i < FRAME_MAX; i++) {
		zassert_equal(p1_parser_feed(&p, 'P', 0, &out), 0);
	}
	/* O 129o byte nao cabe: comeca o descarte, que segue ate o LF. */
	zassert_equal(p1_parser_feed(&p, 'P', 0, &out), 0);
	zassert_true(p.discarding);
	zassert_equal(p.len, 0);
	zassert_equal(p1_linha(&p, " ERROR FRAME\n", 0, &out), 0);
	zassert_false(p.discarding);
	zassert_equal(p1_linha(&p, "P1 ERROR FRAME\n", 0, &out), 1);
	zassert_equal(out.error, P1_ERROR_FRAME);
}

ZTEST(parsers, test_p1_linha_de_128_bytes_e_lf)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};

	for (int i = 0; i < FRAME_MAX; i++) {
		zassert_equal(p1_parser_feed(&p, 'P', 0, &out), 0);
	}
	zassert_equal(p.len, FRAME_MAX);
	zassert_equal(p1_parser_feed(&p, '\n', 0, &out), 0);
	zassert_equal(p.len, 0);
	zassert_false(p.discarding, "o LF que estoura ja fecha o descarte");
	zassert_false(p.have_first);
	zassert_equal(p1_linha(&p, "P1 ERROR UNKNOWN\n", 0, &out), 1);
	zassert_equal(out.error, P1_ERROR_UNKNOWN);
}

ZTEST(parsers, test_p1_reset)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};

	zassert_equal(p1_linha(&p, "P1 ERR", 50, &out), 0);
	zassert_true(p.len > 0);
	p1_parser_reset(&p);
	zassert_equal(p.len, 0);
	zassert_false(p.have_first);
	zassert_false(p.discarding);
	zassert_equal(p1_linha(&p, "P1 ERROR FRAME\n", 50, &out), 1);
	zassert_equal(out.error, P1_ERROR_FRAME);

	p1_parser_reset(NULL);
}

ZTEST(parsers, test_p1_saida_nula_nao_consome)
{
	struct p1_parser p = {0};

	zassert_equal(p1_parser_feed(&p, 'P', 0, NULL), -EINVAL);
	zassert_equal(p.len, 0, "argumento nulo nao consome o byte");
	zassert_false(p.have_first);
}

ZTEST(parsers, test_p1_descarte_sem_marca_de_tempo)
{
	/* Mesmo estado montado a mao do teste da camera. */
	struct p1_parser p = {0};
	struct p1_result out = {0};

	p.discarding = true;
	zassert_equal(p1_linha(&p, "P1 ERROR FRAME\n", 6000, &out), 0);
	zassert_false(p.discarding);
	zassert_false(p.have_first);
	zassert_equal(p1_linha(&p, "P1 ERROR CAPACITY\n", 6000, &out), 1);
	zassert_equal(out.error, P1_ERROR_CAPACITY);
}
