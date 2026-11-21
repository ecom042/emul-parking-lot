# Para o Kauã: decisões fechadas e o que fazer

Do Plácido, branch `feat/protocols`. Complementa
[`contrato-e-divisao.md`](contrato-e-divisao.md) (não repito o que está lá) e
[`plano-protocolos.md`](plano-protocolos.md).

## 1. O que mudou desde o contrato rascunho

- Os 3 pontos em aberto da seção 6 do contrato estão decididos (seção 2).
- `payment_start` agora recebe a **tarifa em centavos**, não o tempo estacionado.
- Entra o evento `EV_TARIFF_OUT_OF_RANGE`; a máquina é quem cobra e decide.
- Já existem os blocos de protocolo e drivers finos (seção 5); falta só juntar
  em `camera`, `payment`, `distance`, `screen`.
- **Os headers mudaram de pasta** (seção 4): agora todos ficam em
  `student/src/include/<camada>/`. Seus arquivos em `fsm/`, `services/` e
  `hw/` vão dar conflito de rename ao mesclar.
- Nenhum header de contrato foi alterado no meu branch; as mudanças de
  `events.h` e da tabela são suas.

## 2. Decisões fechadas

| # | Decisão | O que muda para você |
|---|---|---|
| 1 | **Poll na thread da faixa.** `camera_poll(id, now_ms)` e, na saída, `payment_poll(now_ms)` rodam a cada volta (~1 ms). Nunca bloqueiam. | Você os chama em `fsm/lanes.c`, depois de `lane_process` e do prazo. Nenhum header muda. |
| 2 | **A máquina orquestra a cobrança.** Na ação de entrada do estado de cobrança da saída, nesta ordem: `screen_show_plate(plate)` -> `tariff_cents(parked_ms, &cents)` -> `screen_show_amount(cents)` -> `payment_start(plate, cents)`. | Você escreve essa ação. A tarifa congelada é o argumento; reenvios nunca recalculam. |
| 2a | Assinatura nova: `int payment_start(const char *plate, uint32_t cents);` (substitui `(plate, parked_ms)`). | Seu FFF falsifica esta assinatura. |
| 2b | Se `tariff_cents` devolve `-ERANGE` (acima de 9999 unidades): **não chamar** `payment_start`; postar `EV_TARIFF_OUT_OF_RANGE` na faixa de saída (`events_post` não bloqueia). A ação de entrada não muda de estado sozinha. | Na tabela da saída, a coluna `EV_TARIFF_OUT_OF_RANGE` do estado de cobrança vai para a assistência, o mesmo destino de `EV_PAYMENT_FAILED` (buzzer 100 ms + vermelho). |
| 3 | **`events.h`**: acrescentar `EV_TARIFF_OUT_OF_RANGE` antes de `EV_COUNT`, com comentário Doxygen no estilo do arquivo, e a lista completa da seção 2 do contrato. | Você edita `events.h`. Eu publico `EV_PLATE_READ`, `EV_CAPTURE_FAILED`, `EV_PAID`, `EV_PAYMENT_FAILED`, `EV_ADC_FAULT`. Você publica `EV_BOOT`, `EV_BEAM_*`, `EV_CAR_*`, `EV_TIMEOUT`. |
| 4 | **UART por polling** (`uart_poll_in`), sem interrupção. | Nada a fazer, só saber. |
| 5 | **UUID dentro do `payment`.** O serviço chama `p1_uuid_format(seq, out)` uma vez em `payment_start` e guarda a string para reenvios e CANCEL. | A máquina não toca em UUID. |

Texto sugerido para o novo evento:

```c
EV_TARIFF_OUT_OF_RANGE, /**< exit tariff above 9999 units: no charge was started */
```

`EV_PAYMENT_FAILED` deixa de cobrir "tarifa > 9999"; o comentário no contrato
fica desatualizado, o de `events.h` já nasce certo.

## 3. Checklist (nesta ordem)

1. `events.h`: lista completa do contrato + `EV_TARIFF_OUT_OF_RANGE`.
2. `fsm/lanes.c`: chamar `camera_poll` (as duas faixas) e `payment_poll`
   (saída) a cada volta. Esses serviços só existem quando eu entregar o
   contrato (M0) e a `camera` (M2); até lá, nos testes, use falsos do FFF.
3. Ação de entrada do estado de cobrança da saída: a sequência da decisão 2,
   incluindo o ramo `-ERANGE`.
4. Tabela da saída: coluna `EV_TARIFF_OUT_OF_RANGE` do estado de cobrança.
5. Testes com FFF (falsos de `camera_*`, `payment_*`, `screen_*`,
   `tariff_cents`) verificando a **ordem** das chamadas (`fff.call_history`) e
   o caminho `-ERANGE` (`payment_start` com zero chamadas, evento na fila).
6. `presence` (`EV_BEAM_*`, `EV_CAR_*` só ao cruzar o limiar 683/1024).
7. `sessions` e `tariff` (`tariff_cents` com `-ERANGE` acima de 9999 unidades).
8. `main.c`: `EV_BOOT`, inits, threads (passo 5 do contrato, M1).

## 4. Onde está o padrão

### Nova estrutura de pastas (já no meu branch)

```text
student/src/
├── main.c
├── include/  fsm/  services/  hw/       todos os .h, uma pasta por camada
├── fsm/       CMakeLists.txt  src/      só os .c
├── services/  CMakeLists.txt  src/
└── hw/        CMakeLists.txt  src/
```

- Antes o header ficava em `src/<camada>/include/<camada>/x.h`. Agora é
  `src/include/<camada>/x.h`. O `#include "services/barrier.h"` **não muda**.
- A pasta de include é declarada **uma vez**, no `student/CMakeLists.txt`;
  os `CMakeLists.txt` de camada só listam `.c`. Novo módulo: `.h` em
  `src/include/<camada>/`, `.c` em `src/<camada>/src/`, uma linha no
  `CMakeLists.txt` da camada.
- Nos testes (`tests/unit`, `tests/hw`) o include é `../../src/include` e os
  `.c` continuam em `../../src/<camada>/src/x.c`.
- Ao mesclar: se o git não detectar o rename de um header que você editou,
  aplique a sua edição no arquivo novo e apague o antigo.

### Padrões

| Para | Veja |
|---|---|
| Tabela, guarda, `otherwise`, ação de entrada | `student/src/include/fsm/lane.h` |
| Posse das duas faixas e `events_post` | `student/src/include/fsm/lanes.h` |
| Serviço que publica evento (para o motor, depois avisa) | `student/src/include/services/barrier.h` e `src/barrier.c` |
| Header com Doxygen (`@brief`, `@retval`, `-errno`) | `barrier.h`, `events.h` |
| Teste com FFF (falsos, `DEFINE_FFF_GLOBALS` uma vez por binário, ler a fila real) | `student/tests/unit/barrier/test_barrier.c` |

Convenções: arquivos, identificadores e comentários em **inglês**, documentos
em português; `int` com `0`/`-errno`; `__ASSERT` só para erro de programação.
Um branch por passo a partir do `solucao`; PR pequeno que **o outro revisa**;
`git merge solucao` com frequência; nunca `push --force`. `REGISTRO_IA.md`:
só acrescente no fim. Conflito em `CMakeLists.txt`: manter as duas linhas.

## 5. O que o Plácido já entregou (`feat/protocols`)

| Módulo | Use quando |
|---|---|
| `hw/uart_line.h` | ler os bytes já recebidos sem esperar e escrever um buffer; sem protocolo. |
| `hw/adc4.h` | uma conversão assíncrona do ADC4 por vez (`-EBUSY` enquanto a anterior não terminou). |
| `hw/st7789.h` | reset/init do display, janela e pixels via SPI (init bloqueia ~400 ms). |
| `services/wire.h` | validar número canônico, placa e UUID. |
| `services/line_framer.h` | montar linhas CRLF/LF de até 128 B a partir de bytes, com prazo de 1000 ms. |
| `services/camera_proto.h` | formatar `AT+CAPTURE=<id>` e interpretar a resposta (só sintaxe). |
| `services/p1_proto.h` | formatar CHARGE/CANCEL e interpretar a resposta P1 (só sintaxe). |
| `services/text.h` | formato `BRL r.cc` e fonte 5x7 desenhada linha a linha. |

**Entrego depois** (assinaturas do contrato, com a mudança de `payment_start`):

```c
int camera_init(enum lane_id id);
int camera_start(enum lane_id id);          /* ends with EV_PLATE_READ / EV_CAPTURE_FAILED */
void camera_abandon(enum lane_id id);
void camera_poll(enum lane_id id, int64_t now_ms);
int camera_plate(enum lane_id id, char plate[PLATE_LEN + 1]);

int distance_init(void);                    /* publishes chan_distance */

int payment_init(void);
int payment_start(const char *plate, uint32_t cents);   /* mudou */
void payment_cancel(void);
void payment_poll(int64_t now_ms);

int screen_init(void);
int screen_show_plate(const char *plate);
int screen_show_amount(uint32_t cents);
```

## 6. Marcos

| Marco | Precisa de | Cenários |
|---|---|---|
| M0 | contrato aprovado (este documento + `events.h`) | só compila |
| M1 | seu passo 5 (`states.c`, thread da entrada, `main.c`) | `contradictory-limits`, `partial-startup` |
| M2 | fase 2 dos dois + minha `camera` | os de entrada (`low-confidence`, `camera-timeout`, `jam`, ...) |
| M3 | fase 3 dos dois: minha `payment`/`screen`, sua `sessions`/`tariff`/saída | `normal`, `payment-*`, `reset-payment` |

Até lá cada um testa sozinho: você com FFF no lugar de `camera_*`, `distance`,
`payment_*`, `screen_*`; eu com `events_post` e emuladores do Zephyr.

## 7. Verificação

```sh
python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
  --output build/<nome-novo> --headless <cenario>
python scripts/firmware-coverage.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/coverage-<n>   # meta >= 95%
python scripts/static-check.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/static-<n>     # zero achados
```

Resultado do cenário: `build/<nome>/assessment/results.json`. Use `--output`
novo a cada execução e apague as pastas antigas. Detalhes em
[`../../docs/grading-tools.md`](../../docs/grading-tools.md) e
[`contexts/estado-atual.md`](contexts/estado-atual.md).
