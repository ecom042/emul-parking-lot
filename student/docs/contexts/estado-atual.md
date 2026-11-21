# Estado atual do projeto

Atualizado em 2026-11-21 (fim do branch `feat/exit-lane` do Kauã, marco M3). Atualize este
arquivo ao fim de cada sessão de trabalho. O registro de cada sessão fica em
[`sessoes/`](sessoes/). Para retomar o trabalho do Plácido numa sessão nova,
leia [`proxima-sessao.md`](proxima-sessao.md).

## Onde estamos

- **Branches:** `feat/entry-lane` (M1 + M2, entrada) e `feat/exit-lane` (M3,
  saída), este a partir daquele. As duas faixas funcionam ao mesmo tempo no
  Renode.
- **Cenários: 19 de 19 passam nos dois builds**: o normal (`lab.py`,
  otimizado) e o de cobertura (`firmware-coverage.py`, gcov). Verifique
  sempre os dois: um defeito de tempo na partida só aparecia no build normal
  (corrigido com `INPUT_SETTLE_MS` no `runner`).
- **Pastas:** todos os headers ficam em `student/src/include/<camada>/`; os
  `.c` em `student/src/<camada>/src/`; um `CMakeLists.txt` por camada.
- **Pontos do contrato decididos** (ver seção 6 do contrato): `poll` na
  thread da faixa; a máquina cobra (`payment_start(plate, cents)`);
  `EV_TARIFF_OUT_OF_RANGE`; UART por polling; UUID por contador no `payment`;
  sessões com `k_mutex`, não zbus (decidido pelo Kauã no passo 5).

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
- **zbus** para o ADC compartilhado (`chan_distance`: o `distance` publica e o
  `presence` assina). A **tabela de sessões** usa um `k_mutex` com funções
  diretas (`services/sessions.h`), não o `chan_sessions` previsto no
  diagrama 06: as faixas só consultam quando precisam e ninguém precisa ser
  avisado de mudanças.
- **Partida:** `main.c` só chama `runner_init()` e `runner_start()`
  (`fsm/runner`). Uma tabela `setups[]` descreve o que muda entre as faixas
  (regras, `payment_init`, `screen_init`, `payment_poll`, prioridade 5 na
  entrada e 6 na saída). Uma faixa com falha de inicialização fica em
  assistência, sem `EV_BOOT`; a falha do ADC deixa as duas em assistência.
  O `runner_init()` espera `INPUT_SETTLE_MS` (20 ms) antes do `EV_BOOT`. O
  `screen_init()` (lento) roda no começo da thread da saída, nunca na
  `main` (prioridade 0), para não segurar a entrada.
- **Prazos por estado** na tabela (`lane_def.deadline_ms`): 0 = sem prazo,
  um orçamento em ms, ou `LANE_DEADLINE_KEEP` (o PASSING mantém os 10000 ms
  do OPEN). `runner_step()` aplica `EV_TIMEOUT` direto, sem passar pela fila.
- **Interrupções só publicam:** o handler de um sensor faz `k_work_submit()`
  ou `events_post()`; trabalho de GPIO fica fora da ISR.
- **`CONFIG_ASSERT=y`** no firmware: erro de programação (id inválido) para
  com mensagem.
- Nomes de arquivos e identificadores em inglês; comentários de código em
  inglês; documentos em português.

## O que está pronto

Máquinas (`fsm/states.c`): as duas faixas compartilham as linhas INIT, IDLE,
OPENING, OPEN, PASSING, CLOSING e ASSIST (macros `ROW_*`) e os prazos.

```text
INIT ─BOOT─► IDLE (cancela fechada) | ASSIST_LOCKED
IDLE ─CAR_ARRIVED─► IDENTIFYING (fechada) | ASSIST_LOCKED
IDENTIFYING ─PLATE_READ─► entrada: OPENING se pode entrar, senão ASSIST
                          saída:   CHARGING se tem sessão, senão ASSIST
            ─CAPTURE_FAILED─► ASSIST ; ─CAR_LEFT─► IDLE
CHARGING (saída: placa, tarifa, valor, CHARGE) ─PAID─► OPENING
         ─PAYMENT_FAILED / TARIFF_OUT_OF_RANGE / CAR_LEFT─► ASSIST (CANCEL)
OPENING (2500 ms) ─UPPER_REACHED─► OPEN (verde)
OPEN (10000 ms) ─BEAM_ON─► PASSING
PASSING (mantém) ─feixe livre e carro longe─► CLOSING
CLOSING (2500 ms; entrada cria / saída remove a sessão) ─LOWER_REACHED─► IDLE | IDENTIFYING
        ─BEAM_ON / TIMEOUT─► ASSIST_LOCKED
ASSIST ─carro longe e feixe livre─► IDLE (rearma sozinho se já estiver assim)
todos ─LIMITS_CONFLICT / ADC_FAULT─► ASSIST_LOCKED (só sai com reset)
```

| Camada | Módulo | Testes |
|---|---|---|
| `fsm/` | `lane` (tabela, guarda, `otherwise`, fila, prazos por estado) | `tests/unit/fsm/test_lane.c` |
| `fsm/` | `lanes` (as duas faixas, filas estáticas, `events_post`) | `tests/unit/fsm/test_lanes.c` |
| `fsm/` | `states` (regras das duas faixas) | `tests/fsm/states/` |
| `fsm/` | `runner` (partida, thread da entrada, `runner_step`) | `tests/fsm/runner/` |
| `services/` | `events.h` (lista completa) | — |
| `services/` | `signals`, `barrier`, `camera`, `distance`, `screen`, `payment` | `tests/unit/<módulo>/` |
| `services/` | `presence` (histerese 683/1024 sobre `chan_distance`, feixe) | `tests/unit/presence/` |
| `services/` | `sessions` (tabela de vagas com `k_mutex`) | `tests/unit/sessions/` |
| `services/` | `tariff` (200 + 100 por unidade, `-ERANGE` acima de 9999) | `tests/unit/tariff/` |
| `services/` | `wire`, `line_framer`, `camera_proto`, `p1_proto`, `text` | `tests/unit/<módulo>/` |
| `hw/` | `lane_hw`, `indicator`, `motor`, `sensor`, `uart_line`, `adc4`, `st7789` | `tests/hw/` (emuladores do Zephyr) |

**Suítes de teste:**
- `tests/unit/`: lógica, com FFF no lugar de `hw/`. É uma suíte só, com uma
  subpasta por módulo. O curso só encontra suítes no primeiro nível de
  `student/tests/`.
- `tests/hw/`: módulos de `hw/` sobre drivers emulados do Zephyr. Fica
  separada porque os falsos do FFF de `tests/unit/` colidiriam com o `hw/`
  real.
- `tests/fsm/`: `fsm/states` e `fsm/runner` com FFF no lugar de todos os
  serviços (`fakes/service_fakes.{h,c}`). Fica separada porque em
  `tests/unit/` os serviços são compilados de verdade.

**Última medição:** 19/19 cenários no build normal e no de cobertura; todas
as suítes passam; 99,90% de linhas e 95,93% de branches em `student/src`;
`static-check.py` com 0 achados. Os branches sem cobertura estão
justificados no `REGISTRO_IA.md` (17 no `st7789.c`).

## O que falta e quem faz

Contrato em [`../contrato-e-divisao.md`](../contrato-e-divisao.md).

- **Revisão em dupla** dos PRs `feat/entry-lane` e `feat/exit-lane`; guias em
  [`../para-placido.md`](../para-placido.md).
- **Folga de cobertura (opcional):** um driver SPI falso para os 17 branches
  do `st7789.c` (combinar com o Plácido).
- **`states.c` com cerca de 450 linhas:** pode ser separado em regras
  compartilhadas e por faixa, se a dupla achar que fica mais fácil de
  explicar.
- **Preparação para a sabatina:** cada um explicar e mudar qualquer requisito
  (prazo, limiar, regra de rearme) e ver o cenário passar.

## Como trabalhar (combinado com o Kauã)

1. Antes de implementar, o agente mostra um resumo curto (arquivos novos,
   arquivos alterados, funções) e espera a confirmação.
2. Passos pequenos: um módulo ou um comportamento por vez.
3. Commits: o Plácido autorizou o agente a commitar e dar push nos branches
   dele (2026-11-21), depois de verificar. O Kauã faz os próprios commits; o
   agente só sugere a mensagem. Nunca `push --force`, nunca no
   `main`/`solucao`, e só com arquivos de `student/`.
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
