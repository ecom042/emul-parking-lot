/* SPDX-License-Identifier: Apache-2.0 */
/* Parsers independentes: quadro bom, bordas, descarte e prazo de montagem. */
#include <string.h>

#include <zephyr/ztest.h>

#include "at_parser.h"
#include "p1_parser.h"

static int alimenta_at(struct at_parser *p, const char *s, int64_t *now,
		       struct at_plate *out)
{
	int rc = 0;

	for (const char *q = s; *q != '\0'; q++) {
		int r = at_parser_feed(p, *q, *now, out);

		if (r > 0) {
			rc = r;
		}
	}
	return rc;
}

static int alimenta_p1(struct p1_parser *p, const char *s, int64_t *now,
		       struct p1_result *out)
{
	int rc = 0;

	for (const char *q = s; *q != '\0'; q++) {
		int r = p1_parser_feed(p, *q, *now, out);

		if (r > 0) {
			rc = r;
		}
	}
	return rc;
}

ZTEST_SUITE(parsers, NULL, NULL, NULL, NULL, NULL);

ZTEST(parsers, test_at_quadro_bom)
{
	struct at_parser p = {0};
	struct at_plate out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_at(&p, "+PLATE:17,\"ABC1D23\",93\r\n", &now, &out), 1);
	zassert_equal(out.id, 17);
	zassert_str_equal(out.plate, "ABC1D23");
	zassert_equal(out.confidence, 93);
}

ZTEST(parsers, test_at_ok_e_error_nao_sao_placa)
{
	struct at_parser p = {0};
	struct at_plate out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_at(&p, "OK\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "ERROR\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:18,\"ABC1D23\",85\r\n", &now, &out), 1);
}

ZTEST(parsers, test_at_id_canonico)
{
	struct at_parser p = {0};
	struct at_plate out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_at(&p, "+PLATE:017,\"ABC1D23\",93\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:0,\"ABC1D23\",93\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:1000000000,\"ABC1D23\",93\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:999999999,\"ABC1D23\",93\r\n", &now, &out), 1);
	zassert_equal(out.id, 999999999u);
}

ZTEST(parsers, test_at_placa_e_confianca)
{
	struct at_parser p = {0};
	struct at_plate out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_at(&p, "+PLATE:22,\"abc1d23\",93\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:22,\"ABC1D234\",93\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:22,\"ABC1D23\",101\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:22,\"ABC1D23\",093\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:22,\"ABC1D23\",84\r\n", &now, &out), 1,
		      "o limiar de 85 e decisao da FSM, nao do parser");
	zassert_equal(out.confidence, 84);
}

ZTEST(parsers, test_at_terminador)
{
	struct at_parser p = {0};
	struct at_plate out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_at(&p, "+PLATE:23,\"ABC1D23\",93\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:23,\"ABC1D23\",93\r\r\n", &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:24,\"ABC1D23\",93\r\n", &now, &out), 1);
}

ZTEST(parsers, test_at_dois_quadros_e_fragmentos)
{
	struct at_parser p = {0};
	struct at_plate out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_at(&p, "+PLATE:24,\"ABC1D23\",93\r\n", &now, &out), 1);
	zassert_equal(alimenta_at(&p, "+PLATE:25,\"ABC1234\",90\r\n", &now, &out), 1);
	zassert_str_equal(out.plate, "ABC1234");
}

ZTEST(parsers, test_at_linha_longa_descarta_ate_lf)
{
	struct at_parser p = {0};
	struct at_plate out = {0};
	int64_t now = 0;
	char grande[131];

	memset(grande, 'A', 129);
	grande[129] = '\n';
	grande[130] = '\0';

	zassert_equal(alimenta_at(&p, grande, &now, &out), 0);
	zassert_equal(alimenta_at(&p, "+PLATE:27,\"ABC1D23\",93\r\n", &now, &out), 1);
	zassert_equal(out.id, 27);
}

ZTEST(parsers, test_at_prazo_de_montagem)
{
	struct at_parser p = {0};
	struct at_plate out = {0};
	int64_t now = 1000;

	zassert_equal(alimenta_at(&p, "+PLATE:28,\"ABC1D2", &now, &out), 0);
	now += 1001;
	zassert_equal(alimenta_at(&p, "3\",93\r\n", &now, &out), 0,
		      "linha parcial vencida e descartada");
	zassert_equal(alimenta_at(&p, "+PLATE:29,\"ABC1D23\",93\r\n", &now, &out), 1);
	zassert_equal(out.id, 29);
}

ZTEST(parsers, test_p1_paid_e_cancelled)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_p1(&p,
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n", &now, &out), 1);
	zassert_equal(out.status, P1_STATUS_PAID);
	zassert_equal(out.cents, 400);
	zassert_str_equal(out.uuid, "3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1");

	zassert_equal(alimenta_p1(&p,
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1 CANCELLED 400 BRL\n", &now, &out), 1);
	zassert_equal(out.status, P1_STATUS_CANCELLED);
}

ZTEST(parsers, test_p1_error)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_p1(&p, "P1 ERROR FRAME\n", &now, &out), 1);
	zassert_equal(out.status, P1_STATUS_ERROR);
	zassert_equal(out.error, P1_ERROR_FRAME);
	zassert_equal(out.uuid[0], '\0', "P1 ERROR nao carrega UUID");

	zassert_equal(alimenta_p1(&p, "P1 ERROR CAPACITY\n", &now, &out), 1);
	zassert_equal(out.error, P1_ERROR_CAPACITY);
	zassert_equal(alimenta_p1(&p, "P1 ERROR BOGUS\n", &now, &out), 0);
}

ZTEST(parsers, test_p1_malformados)
{
	struct p1_parser p = {0};
	struct p1_result out = {0};
	int64_t now = 0;

	zassert_equal(alimenta_p1(&p,
		"P1 RESULT 3F2A9C10-7B4E-4D21-9A6F-0C5E8B7D12C1 PAID 400 BRL\n", &now, &out), 0,
		"UUID so em minusculo");
	zassert_equal(alimenta_p1(&p,
		"P1 RESULT 3f2a9c10_7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n", &now, &out), 0);
	zassert_equal(alimenta_p1(&p,
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 0400 BRL\n", &now, &out), 0);
	zassert_equal(alimenta_p1(&p,
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400\n", &now, &out), 0);
	zassert_equal(alimenta_p1(&p,
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\r\n", &now, &out), 0,
		"CR na linha do terminal e erro");
	zassert_equal(alimenta_p1(&p,
		"P1  RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n", &now, &out), 0,
		"dois espacos nao sao um espaco");
	zassert_equal(alimenta_p1(&p,
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n", &now, &out), 1,
		"a linha seguinte volta a valer");
}

ZTEST(parsers, test_argumentos_nulos)
{
	struct at_plate plat = {0};
	struct p1_result pay = {0};

	zassert_equal(at_parser_feed(NULL, 'A', 0, &plat), -EINVAL);
	zassert_equal(at_parser_feed(NULL, 'A', 0, NULL), -EINVAL);
	zassert_equal(p1_parser_feed(NULL, 'P', 0, &pay), -EINVAL);
	zassert_equal(p1_parser_feed(NULL, 'P', 0, NULL), -EINVAL);
}
