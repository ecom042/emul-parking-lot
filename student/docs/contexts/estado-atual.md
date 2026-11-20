# Estado atual do projeto

Atualizado em 2026-11-20 (fim da sessão do Kauã). Atualize este arquivo ao
fim de cada sessão de trabalho. O registro de cada sessão fica em
[`sessoes/`](sessoes/).

## Onde estamos

- **Branch:** `feat/entry-lane-base` (renomeado de `feat/fsm`), a partir do
  `solucao`. PR interno para o `solucao` pendente.
- **O firmware ainda se comporta como o ponto de partida:** os módulos
  existem e estão testados, mas o `main.c` ainda não os chama. Por isso os 19
  cenários do Renode reprovam. É esperado.
- **Próximo passo:** passo 5, os estados `INIT`, `IDLE` e `ASSIST_LOCKED` em
  `fsm/states.c`, a thread da faixa de entrada e o `main.c`. Alvo:
  `contradictory-limits` (portão G3) e `partial-startup`.

## Arquitetura (decidida)

Diagramas em [`../diagrams/`](../diagrams/): 01 visão geral, 02 ciclo de uma
faixa, 03 subprocessos, 06 execução (duas threads e zbus), 07 módulos e
camadas.

- **Três camadas** em `student/src/`, cada uma com `include/<camada>/`,
  `src/` e `CMakeLists.txt`, compiladas no alvo `app` (não em bibliotecas:
  a cobertura do curso só lê `CMakeFiles/app.dir` e só conta `student/src`):
  - `fsm/`: a máquina de estados; só inclui `services/`, nunca `hw/`.
  - `services/`: regras ou combinação de mais de um componente; usa `hw/`.
  - `hw/`: um tipo de componente por módulo, sem regras; só chama drivers
    do Zephyr.
- **Máquina por tabela:** `lane_transition {next, guard, otherwise}`, uma
  função de entrada por estado, uma tabela por faixa (entrada e saída
  compartilham as funções).
- **Duas threads**, uma por faixa; a entrada com prioridade maior. Cada faixa
  tem uma fila de eventos (`uint8_t`).
- **Eventos:** `services/events.h` é a lista única. Os serviços publicam com
  `events_post(lane, event)`; a `fsm/lanes.c` é dona das duas faixas e
  implementa `events_post()`.
- **zbus** para o ADC compartilhado (`chan_distance`, publicar e assinar) e
  para a tabela de sessões (`chan_sessions`, `claim`/`finish`).
- **Interrupções só publicam:** o handler de um sensor faz `k_work_submit()`
  ou `events_post()`; trabalho de GPIO fica fora da ISR.
- **`CONFIG_ASSERT=y`** no firmware: erro de programação (id inválido) para
  com mensagem.
- Nomes de arquivos e identificadores em inglês; comentários de código em
  inglês; documentos em português.

## O que está pronto

| Camada | Módulo | Testes |
|---|---|---|
| `fsm/` | `lane` (tabela, guarda, `otherwise`, fila: `lane_post`, `lane_process`) | `tests/unit/fsm/test_lane.c` (14) |
| `fsm/` | `lanes` (as duas faixas, filas estáticas, `events_post`) | `tests/unit/fsm/test_lanes.c` (3) |
| `services/` | `events.h` (`EV_BOOT`, `EV_UPPER_REACHED`, `EV_LOWER_REACHED`, `EV_LIMITS_CONFLICT`) | — |
| `services/` | `signals` (stop, go, assist com pulso de 100 ms) | `tests/unit/signals/` (10) |
| `services/` | `barrier` (motor + fins de curso; para o motor e depois publica) | `tests/unit/barrier/` (9) |
| `hw/` | `lane_hw`, `indicator`, `motor`, `sensor` | `tests/hw/` (1 + 4 + 4, sobre `gpio-emul`) |

**Suítes de teste:**
- `tests/unit/`: lógica, com FFF no lugar de `hw/`. É uma suíte só, com uma
  subpasta por módulo. O curso só encontra suítes no primeiro nível de
  `student/tests/`.
- `tests/hw/`: módulos de `hw/` sobre drivers emulados do Zephyr
  (`zephyr,gpio-emul` no `app.overlay`). Fica separada porque os falsos do FFF
  de `tests/unit/` colidiriam com o `hw/` real.

**Última medição** (`firmware-coverage.py`, `build/cov-p4b2-01`): todas as
suítes passam; 90,5% de linhas e 74,3% de branches em `student/src`.

**Branches sem cobertura** (para o passo de qualidade):
- o lado "falhou" dos `__ASSERT` em `lane_hw_get()` e `lanes_get()`;
- `!gpio_is_ready_dt()` em `indicator.c` e `sensor.c`;
- erros de driver em `motor.c` (cobríveis com FFF) e em `sensor.c`;
- `main.c`.

**Ainda não rodado:** `scripts/static-check.py`.

## O que falta e quem faz

Ver [`../contrato-e-divisao.md`](../contrato-e-divisao.md) (rascunho, revisar
em dupla antes de seguir).

- **Kauã:** passo 5; prazos (`EV_TIMEOUT`); `presence`; estados do ciclo da
  entrada; `sessions`; `tariff`; `exit_table`.
- **Colega:** `uart_line` + `camera`; `adc4` + `distance`; `payment`,
  `st7789`, `screen`.

## Como trabalhar (combinado com o Kauã)

1. Antes de implementar, o agente mostra um resumo curto (arquivos novos,
   arquivos alterados, funções) e espera a confirmação.
2. Passos pequenos: um módulo ou um comportamento por vez.
3. A cada marco, o agente sugere uma mensagem de commit; **o aluno faz o
   commit**, nunca o agente.
4. Verificação pelas ferramentas do curso (`firmware-coverage.py`,
   `static-check.py`, cenários com `lab.py`); não apagar a pasta de saída de
   uma medição em andamento.
5. Explicar o código ao aluno quando ele pedir; a sabatina é individual e
   cobre o código inteiro.

## Antes da entrega

- Reverter a referência a esta pasta acrescentada no `AGENTS.md` da raiz
  (fora de `student/`; o desafio pede que mudanças fora de `student/` sejam
  desfeitas antes do PR de entrega).
- Os dois alunos com entradas no `student/REGISTRO_IA.md` (portão G4).
