# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| | | |
| | | |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| Kauã Lessa | Claude Code (CLI), com a skill `clean-c-zephyr` e o plugin draw.io | Claude Opus 5.5 (`claude-opus-5-5`); um subagente Claude Sonnet na tentativa descartada (ver abaixo) | desde 2026-11-20 |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### AAAA-MM-DD — título curto

- **Aluno:** quem fez esta tarefa.
- **Requisito(s):** REQ-xx
- **O que pedi:** resumo do pedido.
- **O que a IA propôs:** resumo, arquivos e funções afetados.
- **O que aceitei, mudei ou rejeitei, e por quê:**
- **Como verifiquei:** cenários (nome e resultado), cobertura, testes de
  unidade, análise estática, revisão manual.
- **O que aprendi / o que ainda preciso entender:**

### 2026-11-20 — Entendimento do problema e arquitetura

- **Aluno:** Kauã Lessa
- **Requisito(s):** visão geral de REQ-01 a REQ-13 (sem código)
- **O que pedi:** diagramas de estados de alto nível do problema e, depois,
  uma arquitetura simples de apresentar, com aviso quando houvesse excesso
  de camadas.
- **O que a IA propôs:** diagramas (visão geral, ciclo de uma faixa,
  subprocessos de captura/pagamento/ADC) e duas arquiteturas: laço único com
  `switch` e máquina por tabela com fila de eventos.
- **O que aceitei, mudei ou rejeitei, e por quê:** propus a máquina por
  tabela e uma camada de comandos com ponteiros de função; a IA apontou que a
  camada de comandos não teria assinatura comum e aceitei retirá-la. Escolhi
  duas threads (entrada com prioridade maior), zbus para o ADC e para a
  tabela de sessões (claim/finish) e três camadas: `fsm/` → `services/` →
  `hw/`. Defini a regra de que tudo que combina mais de um componente de
  hardware é serviço, e que `hw/` tem um tipo de componente por módulo
  (`sensor`, `indicator`, `motor`). Nomes de arquivos em inglês.
- **Como verifiquei:** revisão manual contra `docs/desafio.md`,
  `docs/student-specification.md` e `docs/device-protocols.md`; a IA corrigiu
  um erro do diagrama (desistir durante o pagamento vai à assistência, não ao
  repouso) ao reler o protocolo P1.
- **O que aprendi / o que ainda preciso entender:** respostas descartadas da
  câmera e do terminal não gastam tentativa nem reiniciam o prazo; diferença
  entre condições em sequência (subestados) e condições simultâneas (guarda
  que consulta o nível atual).

### 2026-11-20 — Implementação de uma vez, descartada

- **Aluno:** Kauã Lessa
- **Requisito(s):** faixa de entrada (REQ-01 a REQ-11)
- **O que pedi:** que um subagente implementasse o plano inteiro da faixa de
  entrada.
- **O que a IA propôs:** cerca de 2000 linhas em `fsm/`, `services/`, `hw/` e
  cinco suítes de teste.
- **O que aceitei, mudei ou rejeitei, e por quê:** interrompi antes do fim e
  apaguei tudo: era grande demais para eu revisar e defender. Passei a
  trabalhar em passos pequenos, com resumo de arquivos e funções antes de
  cada passo, aprovação minha e commits feitos por mim.
- **Como verifiquei:** não verificado (descartado antes do relatório).
- **O que aprendi / o que ainda preciso entender:** a sabatina exige que eu
  explique cada linha; implementação grande de uma vez não serve.

### 2026-11-20 — Motor genérico da máquina de estados e fila (fsm/lane)

- **Aluno:** Kauã Lessa
- **Requisito(s):** base de todos (máquina de estados)
- **O que pedi:** o motor da máquina por tabela, testado sozinho.
- **O que a IA propôs:** `fsm/lane.h`/`lane.c`: `struct lane_transition`
  (`next`, `guard`, `otherwise`), `struct lane_def`, `lane_init()`,
  `lane_dispatch()`; depois `lane_post()` (não bloqueia, pode ser chamada de
  ISR) e `lane_process()`, com a fila criada fora do `lane.c`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei; pedi o campo
  `id` na `struct lane` depois de entender como a máquina real usa o motor, e
  os testes em subpastas de `tests/unit/` (a IA avisou que o curso só
  encontra suítes no primeiro nível de `tests/`, então `tests/unit/` virou
  uma suíte com uma subpasta por módulo).
- **Como verifiquei:** `tests/unit/fsm/test_lane.c` (14 testes) passando no
  Renode via `scripts/firmware-coverage.py`; `lane.c` com 100% de linhas e
  branches.
- **O que aprendi / o que ainda preciso entender:** `choose_target()` e a
  avaliação em curto-circuito do `||` (a guarda `NULL` nunca é chamada).

### 2026-11-20 — Sinais (hw/indicator, hw/lane_hw, services/signals)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-01, REQ-06, REQ-09 (vermelho, verde, buzzer)
- **O que pedi:** luzes e buzzer, usando a skill `clean-c-zephyr`.
- **O que a IA propôs:** `indicator_init/set`; `lane_hw` com os aliases do
  devicetree; `signals_stop/go/assist`, com pulso de 100 ms do buzzer por
  `k_work_delayable`.
- **O que aceitei, mudei ou rejeitei, e por quê:** decidi que `signals_go()`
  apaga o vermelho e que `enum lane_id` fica em `hw/lane_hw.h`. Perguntei por
  que não devolver o retorno de `k_work_reschedule()`: a IA conferiu que ele
  devolve 1 em caso de sucesso e nunca falha com atraso diferente de zero, e
  trocou por `(void)` + `return 0`. Pedi `CONFIG_ASSERT=y` no firmware para
  que um id inválido pare com mensagem em vez de mexer num pino aleatório.
- **Como verifiquei:** `tests/unit/signals` (10 testes, FFF no lugar do
  `indicator`) passando; `signals.c` 100%; nenhuma asserção do Zephyr
  disparou nos logs dos cenários com `CONFIG_ASSERT=y`.
- **O que aprendi / o que ainda preciso entender:** sem `CONFIG_ASSERT`, um
  `__ASSERT` vira código vazio; cada `__ASSERT` acrescenta um branch que não
  é coberto.

### 2026-11-20 — Suíte da camada hw com GPIO emulado

- **Aluno:** Kauã Lessa
- **Requisito(s):** qualidade (cobertura de `hw/`)
- **O que pedi:** testar o `indicator.c` com `gpio-emul`.
- **O que a IA propôs:** a suíte `tests/hw/` (primeiro nível, encontrada pelo
  curso), com um controlador `zephyr,gpio-emul` no `app.overlay`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. Perguntei por que
  não usar FFF: as funções de GPIO do Zephyr são `static inline` e não podem
  ser falsificadas na ligação; o FFF do `signals` também colidiria com o
  `indicator.c` real no mesmo binário.
- **Como verifiquei:** `unit:hw` passando no `firmware-coverage.py` (o ZVB
  aceitou o nó emulado); `indicator.c` passou de 0 para 5/5 linhas.
- **O que aprendi / o que ainda preciso entender:** o branch
  `!gpio_is_ready_dt()` não é produzido pelo emulador e fica sem cobertura.

### 2026-11-20 — Motor e sensor (hw/motor, hw/sensor)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-06, REQ-08 (base de movimento e fins de curso)
- **O que pedi:** motor e entrada com interrupção.
- **O que a IA propôs:** `motor_init/drive/stop` (reaproveitando o
  `indicator`; direção antes do enable); `sensor_init` com interrupção nas
  duas bordas e `sensor_is_active`; `lane_hw` com motor, fins de curso e
  feixe.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. A primeira
  medição falhou porque o teste mudava o nível de um pino ainda não
  configurado como entrada; a IA corrigiu o teste.
- **Como verifiquei:** `tests/hw` (motor 4, sensor 4) passando; um segundo
  controlador emulado sem bordas cobre o `-ENOTSUP`.
- **O que aprendi / o que ainda preciso entender:** diferença entre a
  `gpio_callback` (inscrição no driver), a `on_edge()` e o handler do dono;
  o handler roda em contexto de interrupção e não pode bloquear (nos testes
  com `gpio-emul` ele roda na thread do teste).

### 2026-11-20 — Eventos e cancela (services/events, fsm/lanes, services/barrier)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-01 (fins de curso contraditórios), REQ-06
- **O que pedi:** que o próprio `barrier` desligue o motor e que os serviços
  publiquem na fila de eventos.
- **O que a IA propôs:** `services/events.h` como lista única de eventos e
  `events_post()`; primeiro com uma função de "bind" no `main.c`.
- **O que aceitei, mudei ou rejeitei, e por quê:** questionei o bind ("se ele
  conhece o id, conhece a lane"); a `fsm/` passou a ser dona das duas faixas
  (`fsm/lanes.c`), e o bind saiu. Também saiu o "objetivo" do `barrier`: o
  trabalho decide pelos níveis atuais dos dois fins de curso. O handler (ISR)
  só faz `k_work_submit()`; o trabalho para o motor e só depois publica o
  evento.
- **Como verifiquei:** `tests/unit/fsm/test_lanes.c` (3) e
  `tests/unit/barrier` (9, FFF no lugar de `motor` e `sensor`, eventos lidos
  da fila real) passando; `barrier.c` 100% de linhas e branches; total do
  projeto 90,5% de linhas e 74,3% de branches (cenários ainda reprovam: o
  `main.c` ainda é o ponto de partida).
- **O que aprendi / o que ainda preciso entender:** `CONTAINER_OF` para
  voltar do `k_work` à faixa; por que parar o motor antes de publicar
  (REQ-06).

### Blocos de protocolo e drivers finos (Plácido, Claude Code)

- **O que pedi:** implementar o plano `docs/plano-protocolos.md`: a parte do
  colega que não depende dos pontos em aberto do contrato.
- **O que a IA propôs e fez:** `services/wire`, `line_framer`, `camera_proto`,
  `p1_proto`, `text` (lógica pura) e `hw/uart_line`, `hw/adc4`, `hw/st7789`,
  cada um com testes. O passo 0 do plano (experimento de perda de bytes por
  polling no Renode) **não foi feito**: `uart_line` usa `uart_poll_in`, e isso
  precisa ser validado na fase de testes com a câmera real.
- **Acréscimo:** adicionado `p1_uuid_format` (UUID determinístico por contador de cobrança) em `p1_proto`, com testes.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher na revisão).
- **Como verifiquei:** `firmware-coverage.py`: todas as suítes passam; 96,4% de
  linhas e 86,3% de branches em `student/src` (cenários ainda reprovam);
  `static-check.py`: 0 achados. O `st7789` só tem teste dos argumentos e do
  caminho pela placa; os pixels dependem do cenário `normal`.
- **O que aprendi / o que ainda preciso entender:** (preencher).

### Etapa 1 (M0): headers do contrato (Plácido, Claude Code)

- **O que pedi:** a etapa 1 das 5 (declarações compartilhadas), planejada por
  um subagente e implementada em seguida.
- **O que a IA fez:** `events.h` completo (com `EV_TARIFF_OUT_OF_RANGE`),
  campos `camera` e `distance_cfg` e `payment_terminal_get()` em `lane_hw`,
  headers `camera.h`, `distance.h`, `payment.h`, `screen.h`, `CONFIG_ZBUS=y`
  e `tests/unit/lane_hw`. Nenhum serviço implementado.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher). Os headers
  `presence.h`, `sessions.h` e `tariff.h` ficam com o Kauã.
- **Como verifiquei:** os 4 headers compilados num `main.c` temporário
  (revertido); suítes passam, 96,4% de linhas e 86,3% de branches;
  `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** (preencher).

### Etapa 2: services/camera (Plácido, Claude Code)

- **O que pedi:** implementar `services/camera` (3 tentativas com ID novo,
  1000 ms cada, confiança >= 85), planejado por um Opus e implementado por um
  Sonnet.
- **O que a IA fez:** `camera.c` (máquina IDLE/SEND/WAITING, envio no primeiro
  `poll` depois do `start`, prazo `>= 1000 ms`, placa limpa no `start`) e 20
  testes ztest+FFF em `tests/unit/camera/`.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher). Os 2 branches
  dos `__ASSERT` ficaram sem exclusão; decisão pendente.
- **Como verifiquei:** suítes passam; `camera.c` 100% de linhas e 92,86% de
  branches; total 96,85% / 86,81%; `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** (preencher).

### Etapa 3: services/distance (Plácido, Claude Code)

- **O que pedi:** implementar `services/distance` (ADC4 alternando os canais
  17/18, uma conversão por vez, prazo de 100 ms, `chan_distance` no zbus, falha
  trava e posta `EV_ADC_FAULT`), planejado por um Opus e implementado por um
  Sonnet.
- **O que a IA fez:** `distance.c` (IDLE/PENDING/LATCHED, `k_work_delayable` de
  5 ms, conclusão em 100 ms ou mais conta como falha), `distance_adc_get()` em
  `lane_hw`, `distance_start()`/`distance_poll()` no header e 16 testes.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher). Aceitei
  separar `init`/`start`/`poll` (muda o contrato: o `main.c` chama
  `distance_start()`); falha de `init` trava e devolve o erro sem postar.
- **Como verifiquei:** suítes passam; `distance.c` 100% de linhas e branches;
  total 97,17% / 87,47%; `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** (preencher).

### Etapa 4: services/screen (Plácido, Claude Code)

- **O que pedi:** implementar `services/screen` (`screen_init`,
  `screen_show_plate`, `screen_show_amount`) sobre `hw/st7789` e `text`,
  planejado por um Opus e implementado por um Sonnet.
- **O que a IA fez:** `screen.c` (textos centralizados, `BRL x.xx`,
  `SIMULATED PAYMENT`, `NOT PIX`; limpa só a área de cobrança, y 40..167;
  entrada inválida devolve -EINVAL sem SPI), `payment_display_get()` em
  `lane_hw` e 17 testes.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher). No caminho
  -ERANGE a máquina não deve chamar `screen_show_amount` (nota no
  `para-kaua.md`).
- **Como verifiquei:** suítes passam; `screen.c` 100% de linhas e branches;
  total 97,35% / 88,15%; `static-check.py` com 0 achados. Pixels só serão
  vistos quando o `main.c` chamar o serviço.
- **O que aprendi / o que ainda preciso entender:** (preencher).

### Etapa 5: services/payment (Plácido, Claude Code)

- **O que pedi:** implementar `services/payment` (UUID por contador, 3 envios
  de 1000 ms, só PAID exato, CANCEL no fim), planejado por um Opus e
  implementado por um Sonnet.
- **O que a IA fez:** `payment.c` (IDLE/SEND/WAITING; PAID e CANCELLED só
  valem com o UUID e o valor da cobrança atual; no 3º prazo invalida, envia
  CANCEL e só então posta `EV_PAYMENT_FAILED`) e 23 testes.
- **O que aceitei, mudei ou rejeitei, e por quê:** (preencher). CANCELLED atual
  posta FAILED sem reenviar CANCEL; ERROR do terminal é ignorado e o prazo
  resolve.
- **Como verifiquei:** suítes passam; `payment.c` 100% de linhas e 97,06% de
  branches (falta só o `__ASSERT` do CANCEL); total 97,61% / 88,84%;
  `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** (preencher).

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| | | |
