# Contrato entre módulos e divisão do trabalho (rascunho)

Rascunho para o Kauã e o colega revisarem **juntos**, antes de cada um seguir
no seu branch. Nada aqui foi implementado. Depois de aprovado, os headers
abaixo entram no `solucao` num PR próprio ("contrato"), só com as declarações.
A partir daí, mudar um header de contrato é algo combinado entre os dois.

Base: branch `feat/entry-lane-base` (fsm/lane, fsm/lanes, signals, barrier,
motor, sensor, indicator, lane_hw), diagramas 06 e 07 em
`student/docs/diagrams/`.

---

## 1. Divisão

Divide por fronteira, não por faixa: entrada e saída usam o mesmo código.

| | Kauã: máquina e regras | Colega: periféricos de dados e protocolos |
|---|---|---|
| Já feito | `fsm/lane`, `fsm/lanes`, `signals`, `barrier`, `hw/` de GPIO | — |
| Fase 1 | passo 5: `fsm/states.c` (INIT, IDLE, ASSIST_LOCKED), thread da entrada, `main.c` | `hw/uart_line` + `services/camera` |
| Fase 2 | prazos na `fsm`, `services/presence`, estados do ciclo da entrada (com FFF no lugar de `camera`) | `hw/adc4` + `services/distance` |
| Fase 3 | `services/sessions`, `services/tariff`, `exit_table`, thread da saída | `services/payment`, `hw/st7789`, `services/screen` |

### Marcos de integração (merge no `solucao` e cenários)

| Marco | Precisa de | Cenários que devem passar |
|---|---|---|
| M0 | contrato aprovado | — (só compila) |
| M1 | passo 5 (Kauã) | `contradictory-limits`, `partial-startup` |
| M2 | fase 2 dos dois + `camera` | os 13 só de entrada: `low-confidence`, `camera-timeout`, `reset-capture`, `jam`, `missing-upper`, `missing-lower`, `stationary`, `noisy-threshold`, `confidence-retry`, `obstruction-before-close`, `obstruction-during-close` |
| M3 | fase 3 dos dois | `normal`, `payment-delayed`, `payment-missing`, `payment-duplicate`, `payment-stale`, `reset-payment` |

Até o marco de integração, cada um testa sozinho: o Kauã com falsos do FFF no
lugar de `camera_*`, `distance` e `payment_*`; o colega com `events_post()`
falsificada (ou real, lendo a fila como em `tests/unit/barrier`) e com os
emuladores do Zephyr (`zephyr,uart-emul`, `zephyr,adc-emul`) em `tests/hw/`.

### Regras de trabalho

- Um branch por passo, a partir do `solucao`; PR pequeno; **o outro revisa**
  (a sabatina cobre o código inteiro, inclusive o do colega).
- Atualizar o branch com `git merge solucao` com frequência; nunca `push --force`.
- Cada um commita com a sua identidade do git.
- `REGISTRO_IA.md`: cada um só acrescenta entradas no fim (conflito trivial).
- Conflitos esperados nos `CMakeLists.txt` (listas de arquivos): resolver
  mantendo as duas linhas.
- Os diagramas e este documento estão em `student/docs/` (versionados).
  Os contextos para agentes ficam em `student/docs/contexts/`.

---

## 2. Eventos: `services/events.h` (lista completa)

O arquivo que mais daria conflito. Fechar a lista agora; cada evento diz quem
publica.

```c
enum lane_event {
	/* main.c */
	EV_BOOT,            /**< firmware ready: leave INIT */

	/* barrier (feito) */
	EV_UPPER_REACHED,   /**< upper limit reached; motor already off */
	EV_LOWER_REACHED,   /**< lower limit reached; motor already off */
	EV_LIMITS_CONFLICT, /**< both limits active; motor already off */

	/* presence (Kauã) */
	EV_BEAM_ON,         /**< something entered the beam */
	EV_BEAM_OFF,        /**< the beam is clear again */
	EV_CAR_ARRIVED,     /**< fresh distance code crossed below 683 */
	EV_CAR_LEFT,        /**< fresh distance code crossed above 1024 */

	/* distance (colega): posted to BOTH lanes */
	EV_ADC_FAULT,       /**< conversion error, invalid sample or > 100 ms */

	/* camera (colega) */
	EV_PLATE_READ,      /**< valid plate, confidence >= 85, current capture ID */
	EV_CAPTURE_FAILED,  /**< three attempts used up */

	/* payment (colega), exit lane only */
	EV_PAID,            /**< exact PAID for the current UUID and amount */
	EV_PAYMENT_FAILED,  /**< no PAID after 3 sends or CANCELLED; the UUID is already cancelled */

	/* fsm (Kauã) */
	EV_TIMEOUT,         /**< the current state's deadline expired */
	EV_TARIFF_OUT_OF_RANGE, /**< tariff_cents() said -ERANGE (> 9999 units); no charge was sent */

	EV_COUNT,
};
```

`EV_TARIFF_OUT_OF_RANGE` é publicado pela própria máquina da saída (ação de
entrada do estado de cobrança, via `events_post` na sua faixa), não por um
serviço. Leva à assistência, como `EV_PAYMENT_FAILED`.

Regra: um serviço publica um evento **depois** de deixar o seu próprio estado
coerente (como o `barrier`, que para o motor antes de avisar).

---

## 3. Hardware: `hw/lane_hw.h` (campos novos)

Proposta; o colega finaliza ao implementar `uart_line` e `adc4`.

```c
struct lane_hw {
	/* ... motor, limit_upper, limit_lower, beam, red, green, buzzer (feitos) */
	const struct device *camera;         /**< USART3 (entry) / USART2 (exit) */
	struct adc_channel_cfg distance_cfg; /**< ADC4 channel 17 (entry) / 18 (exit) */
};

/* Exit-lane only, outside struct lane_hw. */
const struct device *payment_terminal_get(void); /**< LPUART1 */
```

Aliases já existentes no devicetree: `entry-camera`, `exit-camera`,
`entry-distance`, `exit-distance`, `payment-terminal`, `payment-display`.

---

## 4. Serviços: o que a máquina chama

Convenção de todos: `int` com `0` ou `-errno`; tudo o que demora termina com
um **evento**, nunca bloqueando quem chamou.

### 4.1 `services/camera.h` (colega)

```c
#define PLATE_LEN 7 /**< "ABC1D23": [A-Z]{3}[0-9][A-Z0-9][0-9]{2} */

/** UART setup for the lane's camera. Before the lane thread starts. */
int camera_init(enum lane_id id);

/**
 * Start a recognition: up to 3 attempts, a fresh capture ID each, 1000 ms per
 * attempt. Ends with EV_PLATE_READ or EV_CAPTURE_FAILED for this lane.
 */
int camera_start(enum lane_id id);

/** Stop the current recognition; late replies for its IDs are ignored. */
void camera_abandon(enum lane_id id);

/**
 * Service the camera: read received bytes, assemble lines (128 B max,
 * 1000 ms assembly deadline), check attempt deadlines. Called by the lane
 * thread on every loop (about every 1 ms); never blocks.
 */
void camera_poll(enum lane_id id, int64_t now_ms);

/** The plate of the last EV_PLATE_READ. @retval -ENOENT if none. */
int camera_plate(enum lane_id id, char plate[PLATE_LEN + 1]);
```

### 4.2 `services/distance.h` (colega)

```c
/** One fresh ADC4 sample of one lane. */
struct distance_sample {
	enum lane_id lane; /**< which channel: 17 = entry, 18 = exit */
	uint16_t code;     /**< raw 12-bit code: 0 = 0 mm, 4095 = 6000 mm or more */
	int64_t taken_ms;  /**< virtual time of completion */
};

ZBUS_CHAN_DECLARE(chan_distance); /**< publishes struct distance_sample */

/**
 * Start the periodic acquisition: one conversion at a time, alternating
 * channels, completion checked without blocking and observed within 100 ms.
 * Any fault posts EV_ADC_FAULT to both lanes and stops acquisition (latched).
 */
int distance_init(void);
```

Os limiares 683 e 1024 **não** ficam aqui: são regra de presença.

### 4.3 `services/presence.h` (Kauã)

```c
/** Beam sensor and subscription to chan_distance for this lane. */
int presence_init(enum lane_id id);

/** Latest fresh sample below 683 (car at the identification point). */
bool presence_car_near(enum lane_id id);

/** Latest fresh sample above 1024 (car gone). */
bool presence_car_far(enum lane_id id);

/** Beam currently active. */
bool presence_beam_active(enum lane_id id);
```

Publica `EV_CAR_ARRIVED`/`EV_CAR_LEFT` só ao **cruzar** o limiar (histerese),
e `EV_BEAM_ON`/`EV_BEAM_OFF`.

### 4.4 `services/sessions.h` e `services/tariff.h` (Kauã)

```c
/** Free spot and plate not already parked. */
bool sessions_can_enter(const char *plate);

/** @retval -ENOSPC lot full. @retval -EEXIST plate already parked. */
int sessions_create(const char *plate, int64_t entry_ms);

/** @retval -ENOENT no session for this plate. */
int sessions_entry_time(const char *plate, int64_t *entry_ms);

/** @retval -ENOENT no session for this plate. */
int sessions_remove(const char *plate);
```

```c
/**
 * 200 + (units - 1) * 100 centavos, units = max(1, ceil(parked / unit_ms)).
 * @retval -ERANGE above 9999 units (request assistance, never overflow).
 */
int tariff_cents(int64_t parked_ms, uint32_t *cents);
```

### 4.5 `services/payment.h` e `services/screen.h` (colega), só na saída

**Quem cobra é a máquina** (decidido): a ação de entrada do estado de cobrança
da saída chama, em ordem, `screen_show_plate(plate)`, `tariff_cents(parked_ms,
&cents)`, `screen_show_amount(cents)` e `payment_start(plate, cents)`. Se
`tariff_cents` devolver `-ERANGE`, não chama `payment_start` e faz
`events_post(LANE_EXIT, EV_TARIFF_OUT_OF_RANGE)`. A tarifa congelada é o
argumento `cents`: reenvios nunca recalculam. O `payment` só conhece o
protocolo P1 e não depende de `tariff` nem de `screen`.

```c
/** LPUART1 setup. Before the exit lane thread starts. */
int payment_init(void);

/**
 * Send P1 CHARGE for a charge the machine already froze: new UUID (counter
 * inside this service, see p1_uuid_format), then up to 3 sends of the same
 * frame, 1000 ms each, same UUID and same amount. Ends with EV_PAID or
 * EV_PAYMENT_FAILED on LANE_EXIT. Never calls screen_* or tariff_cents.
 *
 * @param cents amount in centavos, already frozen by the machine (1..1000000).
 */
int payment_start(const char *plate, uint32_t cents);

/** Invalidate the current UUID, then send P1 CANCEL (car left / give up). */
void payment_cancel(void);

/** Read terminal bytes and check deadlines; called by the exit lane thread. */
void payment_poll(int64_t now_ms);
```

```c
/** Hardware reset and ST7789V init sequence (about 400 ms, blocks).
 *  Must run in main() before the lane threads start. */
int screen_init(void);

/** Charge screen, step 1: the recognized plate. */
int screen_show_plate(const char *plate);

/** Charge screen, step 2: "BRL r.cc", "SIMULATED PAYMENT", "NOT PIX". */
int screen_show_amount(uint32_t cents);
```

---

## 5. Thread da faixa (Kauã)

A `fsm/lanes.c` cria as threads. Em cada volta (`lane_process(lane, K_MSEC(1))`):

1. aplica um evento da fila, se houver;
2. confere o prazo do estado e publica `EV_TIMEOUT`;
3. chama `camera_poll(id, now)` e, na saída, `payment_poll(now)`.

É por isso que `camera_poll` e `payment_poll` **não podem bloquear**.

---

## 6. Pontos que estavam em aberto (decididos)

1. `camera_poll`/`payment_poll` são chamados **pela thread da faixa**, a cada
   volta (~1 ms), como na seção 5. Cada faixa tem a sua thread e a sua UART,
   então uma faixa travada não atrasa a outra; sem mutex.
2. **A máquina cobra** (seção 4.5): `payment_start(plate, cents)`.
3. Tarifa acima de 9999 unidades tem **evento próprio**,
   `EV_TARIFF_OUT_OF_RANGE` (seção 2): nenhum CHARGE foi enviado, então não se
   mistura com `EV_PAYMENT_FAILED`.
4. Recepção da UART por **polling** (`uart_poll_in`), sem interrupção. Se um
   cenário mostrar bytes perdidos, encurtar o período antes de pensar em IRQ.
5. O **UUID** é um contador determinístico dentro do `payment`
   (`p1_uuid_format`), gerado uma vez por cobrança; reenvios e CANCEL reusam a
   string. A placa não tem fonte de entropia.
6. Diagramas em `student/docs/diagrams/`; headers em `student/src/include/`.
