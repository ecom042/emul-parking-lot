/*
 * camera.h — Máquina de captura da câmera de placas (uma por faixa)
 * =================================================================
 *
 * PARA QUE SERVE?
 *   Implementa a METADE "câmera" da interface `struct parking_equipment`
 *   (src/parking.h), que a Maria Antonia já usa no controle:
 *
 *       capture_start  -> camera_capture_start()
 *       capture_poll   -> camera_capture_poll()
 *       capture_cancel -> camera_capture_cancel()
 *
 *   O controle (parking.c) só diz "capture" e pergunta "já tem placa?".
 *   TUDO o que é protocolo fica aqui dentro: ids, tentativas, prazos,
 *   confiança mínima. Isso é o "O firmware decide" do desafio.
 *
 * REGRAS IMPLEMENTADAS (REQ-02, REQ-03 na parte da câmera, REQ-11):
 *   - Cada tentativa envia  AT+CAPTURE=<id>\r\n  com um id NOVO (1, 2, 3...).
 *   - Cada tentativa tem 1000 ms de prazo para a resposta COMPLETA
 *     (a resposta chega em pedaços, então o prazo inclui a montagem).
 *   - São no máximo 3 tentativas. Confiança < 85 ou prazo vencido gasta uma
 *     tentativa. Esgotadas: EQUIPMENT_FAILED (o controle pede assistência).
 *   - Só vale um +PLATE com o id da tentativa ATUAL, placa válida e
 *     confiança >= 85 ("OK" e "ERROR" NÃO aceitam nada e NÃO reiniciam o
 *     prazo). Resposta de id antigo, malformada ou fora do prazo é ignorada.
 *
 * COMO O CONTROLE USA (resumo do contrato):
 *   1. camera_capture_start(lane, now)  -> 0 se a captura começou.
 *   2. a cada passo:  r = camera_capture_poll(lane, now, plate)
 *        EQUIPMENT_PENDING -> ainda esperando
 *        EQUIPMENT_OK      -> `plate` tem a placa aceita (texto de 7 + '\0')
 *        EQUIPMENT_FAILED  -> tentativas esgotadas ou erro: pedir assistência
 *      OK e FAILED são "de uma vez só": depois deles a captura acabou.
 *   3. camera_capture_cancel(lane)  -> abandono (carro foi embora): invalida
 *      o id atual NA HORA; qualquer resposta tardia dele nunca autoriza nada.
 *
 * TEMPO: `now` é sempre o uptime virtual do Zephyr (k_uptime_get(), em ms).
 *        Este módulo nunca lê relógio nenhum por conta própria.
 */
#ifndef PARKING_CAMERA_H
#define PARKING_CAMERA_H

#include <stdint.h>

#include "equipment_types.h"

#define CAMERA_ATTEMPTS 3             /* tentativas por captura */
#define CAMERA_ATTEMPT_TIMEOUT_MS 1000 /* prazo de cada tentativa */
#define CAMERA_MIN_CONFIDENCE 85      /* confiança mínima aceita (>=) */

/* Cada chamada de poll lê no máximo CAMERA_READ_ROUNDS blocos de CAMERA_READ_CHUNK bytes. */
#define CAMERA_READ_CHUNK 16U
#define CAMERA_READ_ROUNDS 8

/* Prepara o módulo: zera o estado e confere as UARTs. 0 ou -errno. Chamar no boot. */
int camera_init(void);

/* Esquece capturas, ids e bytes parciais (REQ-13: reset coerente). */
void camera_reset(void);

/* Começa uma captura na faixa. 0 = iniciada; -EINVAL, -EIO ou -ENOSPC se falhou. */
int camera_capture_start(enum parking_lane_id lane, int64_t now);

/* Avança a captura. Veja o contrato no topo deste arquivo. */
enum equipment_result camera_capture_poll(enum parking_lane_id lane, int64_t now,
					  char plate[PARKING_PLATE_SIZE]);

/* Abandona a captura em andamento (não existe comando de cancelamento no protocolo). */
void camera_capture_cancel(enum parking_lane_id lane);

/* Só para testes: força o próximo id (para exercitar o limite 999999999 sem 1 bilhão de capturas). */
void camera_set_next_id(enum parking_lane_id lane, uint32_t id);

#endif /* PARKING_CAMERA_H */
