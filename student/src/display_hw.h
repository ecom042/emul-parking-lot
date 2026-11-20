/*
 * display_hw.h — Camada FINA de hardware do display ST7789V (SPI1 + GPIOs D/C e reset)
 * ===================================================================================
 *
 * PARA QUE SERVE?
 *   É a única fronteira entre a lógica do display (display.c) e os drivers reais
 *   do Zephyr (SPI e GPIO). Os testes de unidade trocam estas funções por falsos
 *   FFF — e por um "painel falso" que confere as regras do modelo.
 *
 * AS TRÊS LINHAS DO DISPLAY:
 *   - SPI (SCK/MOSI): leva os bytes. O chip select (CS, ativo baixo) é controlado
 *     pelo PRÓPRIO driver SPI do Zephyr: cada spi_write é uma "transação".
 *   - D/C (data/command): nível BAIXO = o byte é um COMANDO; ALTO = é DADO
 *     (parâmetro de comando ou pixel). Quem muda é o firmware, antes de cada envio.
 *   - RESET (ativo baixo): o modelo exige uma borda de descida (alto -> baixo)
 *     antes de aceitar qualquer comando; começa "preso em reset".
 *
 * Retornos: 0 ou -errno (-ENODEV: dispositivo não pronto; -EIO: falha de driver).
 */
#ifndef PARKING_DISPLAY_HW_H
#define PARKING_DISPLAY_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Confere SPI e GPIOs e deixa as linhas em repouso: D/C em "comando" e RESET
 * SOLTO (nível alto). Soltá-lo agora é o que permite ao modelo ver depois a
 * borda de descida do reset.
 */
int display_hw_init(void);

/* Segura (asserted = true, nível baixo) ou solta (false, nível alto) a linha de reset. */
int display_hw_reset(bool asserted);

/* Envia um byte de COMANDO (D/C baixo) e, se length > 0, seus parâmetros (D/C alto). */
int display_hw_command(uint8_t command, const uint8_t *params, size_t length);

/* Envia dados (D/C alto): pixels logo depois de um RAMWR. */
int display_hw_data(const uint8_t *data, size_t length);

#endif /* PARKING_DISPLAY_HW_H */
