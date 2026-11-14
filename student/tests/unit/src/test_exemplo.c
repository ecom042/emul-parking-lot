/*
 * Exemplo de suíte ztest + FFF. Apague-o quando tiver as suas suítes.
 *
 * Ideia: o código de controle chama uma camada fina de hardware sua (por
 * exemplo, uma função que liga o motor). No firmware, essa camada chama os
 * drivers do Zephyr. No teste, o FFF a substitui por uma função falsa, que
 * registra as chamadas e devolve o que você quiser, inclusive erros que os
 * drivers reais nunca produzem no laboratório (-EIO, -EBUSY...). Assim os
 * caminhos de erro também são testados e cobertos.
 *
 * Aqui a "camada de hardware" e o "código de controle" estão no próprio
 * arquivo só para o exemplo compilar sozinho.
 */
#include <errno.h>
#include <stdbool.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

/** Camada de hardware falsa: no projeto real, declarada num .h seu. */
FAKE_VALUE_FUNC(int, exemplo_motor_ligar, bool);

/** "Código de controle" sob teste: liga o motor e repassa um erro. */
static int exemplo_abrir(void)
{
	int rc = exemplo_motor_ligar(true);

	if (rc != 0) {
		return rc;
	}
	return 0;
}

static void reiniciar_falsos(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(exemplo_motor_ligar);
	FFF_RESET_HISTORY();
}

ZTEST_SUITE(exemplo, NULL, NULL, reiniciar_falsos, NULL, NULL);

ZTEST(exemplo, test_abrir_liga_o_motor)
{
	zassert_ok(exemplo_abrir());
	zassert_equal(exemplo_motor_ligar_fake.call_count, 1);
	zassert_true(exemplo_motor_ligar_fake.arg0_val);
}

ZTEST(exemplo, test_abrir_repassa_erro_do_driver)
{
	exemplo_motor_ligar_fake.return_val = -EIO;
	zassert_equal(exemplo_abrir(), -EIO);
}
