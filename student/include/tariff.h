/*
 * tariff.h — Cálculo da tarifa (REQ-12)
 * =====================================
 *
 * PARA QUE SERVE?
 *   Transforma "quanto tempo o carro ficou estacionado" em "quantos centavos
 *   cobrar". É uma função matemática pura: sem UART, sem display, sem relógio.
 *
 * A REGRA (do desafio):
 *       unidades = max(1, ceil(tempo_estacionado / unidade_ms))
 *       centavos = 200 + (unidades - 1) * 100
 *   - "ceil" = arredonda PARA CIMA: começou outra unidade, paga a unidade.
 *   - O tempo conta desde a PASSAGEM na entrada (virtual, em ms).
 *   - Acima de 9999 unidades não se cobra: o valor máximo do terminal é
 *     1.000.000 centavos, e 9999 unidades dão exatamente 200 + 9998*100 =
 *     1.000.000. Passou disso => assistência (função devolve false).
 *
 * EXEMPLO (unidade de 1000 ms, como nos cenários):
 *       2,5 s estacionado -> ceil(2500/1000) = 3 unidades -> 200 + 2*100 = 400
 *       (isto é, "BRL 4.00")
 *
 * POR QUE O `unit_ms` É PARÂMETRO (e não CONFIG_ direto aqui)?
 *   Para testar várias unidades sem recompilar. Quem chama (payment.c) passa
 *   CONFIG_PARKING_TARIFF_UNIT_MS.
 *
 * TUDO EM INTEIROS: nada de float. Dinheiro em centavos inteiros.
 */
#ifndef PARKING_TARIFF_H
#define PARKING_TARIFF_H

#include <stdbool.h>
#include <stdint.h>

#define TARIFF_FIRST_UNIT_CENTAVOS 200U
#define TARIFF_NEXT_UNIT_CENTAVOS 100U
#define TARIFF_MAX_UNITS 9999U

/*
 * Calcula a tarifa entre `entered_at` (passagem na entrada) e `now`, ambos em ms
 * virtuais. Devolve true e grava os centavos em *amount; devolve false se
 * `unit_ms` for 0 ou se passar de 9999 unidades (nesse caso *amount não muda).
 * Tempo negativo (relógio "voltou") é tratado como 0 => 1 unidade.
 */
bool tariff_compute(int64_t entered_at, int64_t now, uint32_t unit_ms, uint32_t *amount);

#endif /* PARKING_TARIFF_H */
