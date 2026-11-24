# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| Plácido Cordeiro | 22210591 | `services/wire`, `line_framer`, `camera_proto`, `p1_proto`, `text`, `camera`, `distance`, `screen`, `payment`; `hw/uart_line`, `adc4`, `st7789`; headers do contrato (`events.h`, `camera.h`, `distance.h`, `payment.h`, `screen.h`) |
| Kauã Lessa | 22210592 | `fsm/` (máquina, estados, threads, `main.c`), `signals`, `barrier`, `presence`, `sessions`, `tariff`, `hw/indicator`, `motor`, `sensor` |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| Kauã Lessa | Claude Code (CLI), com a skill `clean-c-zephyr` e o plugin draw.io | Claude Opus 5.5 (`claude-opus-5-5`); um subagente Claude Sonnet na tentativa descartada (ver abaixo) | desde 2026-11-20 |
| Plácido Cordeiro | Claude Code (CLI, extensão do VS Code); subagentes Opus (planejamento) e Sonnet (implementação) | Claude Sonnet 5.5 (`claude-sonnet-5-5`) e Claude Opus nos subagentes | 2026-11-21 |

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

### 2026-11-21 — Blocos de protocolo e drivers finos (wire, framers, parsers, text, uart_line, adc4, st7789)

- **Aluno:** Plácido Cordeiro
- **Requisito(s):** REQ-10, REQ-11, REQ-12 (base de REQ-02 e REQ-05)
- **O que pedi:** implementar o plano `docs/plano-protocolos.md`: a parte da
  dupla que não depende dos pontos em aberto do contrato.
- **O que a IA propôs:** `services/wire`, `line_framer` (linhas CRLF/LF de até
  128 B, prazo de 1000 ms), `camera_proto`, `p1_proto`, `text` (formato
  `BRL r.cc` e fonte 5x7), todos lógica pura, e os drivers finos `hw/uart_line`,
  `hw/adc4` e `hw/st7789`, cada um com testes. Depois acrescentou
  `p1_uuid_format` (UUID determinístico por contador de cobrança) em `p1_proto`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a separação entre
  protocolo (só sintaxe, sem estado) e serviço (regras e prazos). O passo 0 do
  plano (experimento de perda de bytes por polling da UART no Renode) **não foi
  feito**: `uart_line` usa `uart_poll_in` e isso fica para a integração com a
  câmera real.
- **Como verifiquei:** `firmware-coverage.py`: todas as suítes passam; 96,4%
  de linhas e 86,3% de branches; `static-check.py`: 0 achados. O `st7789` só
  tem teste dos argumentos; os pixels dependem do cenário `normal`.
- **O que aprendi / o que ainda preciso entender:** por que o framer descarta
  tudo até o LF quando a linha é malformada ou longa demais, e por que o
  prazo de 1000 ms é de montagem da linha e não de resposta; como o `adc4`
  mantém buffer e `sequence` estáticos para sobreviver a uma conclusão
  atrasada.

### 2026-11-21 — Etapa 1 (M0): headers do contrato

- **Aluno:** Plácido Cordeiro
- **Requisito(s):** contrato entre os dois lados (todos os REQ que usam eventos)
- **O que pedi:** a etapa 1 das 5 (declarações compartilhadas), planejada por
  um subagente e implementada em seguida.
- **O que a IA propôs:** `events.h` completo (com `EV_TARIFF_OUT_OF_RANGE`),
  campos `camera` e `distance_cfg` e `payment_terminal_get()` em `lane_hw`,
  headers `camera.h`, `distance.h`, `payment.h`, `screen.h`, `CONFIG_ZBUS=y` e
  `tests/unit/lane_hw`. Nenhum serviço implementado.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei fechar os pontos do
  contrato antes de codificar: `poll` na thread da faixa, a máquina cobra
  (`payment_start(plate, cents)`), UART por polling, UUID por contador dentro
  do `payment`. Os headers `presence.h`, `sessions.h` e `tariff.h` ficaram com
  o Kauã.
- **Como verifiquei:** os 4 headers compilados num `main.c` temporário
  (revertido); suítes passam, 96,4% de linhas e 86,3% de branches;
  `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** a regra do contrato de
  deixar o estado coerente **antes** de publicar o evento, e por que só
  `lanes.c` implementa `events_post`.

### 2026-11-21 — Etapa 2: services/camera

- **Aluno:** Plácido Cordeiro
- **Requisito(s):** REQ-02, REQ-11
- **O que pedi:** implementar `services/camera` (3 tentativas com ID novo,
  1000 ms cada, confiança >= 85), planejado por um subagente Opus e
  implementado por um subagente Sonnet.
- **O que a IA propôs:** `camera.c` com máquina IDLE/SEND/WAITING e uma leitura
  de até 32 bytes por `poll`; `camera_start` só arma e o `AT+CAPTURE` sai no
  primeiro `poll`; prazo `now - sent >= 1000`; confiança < 85 parte para a
  próxima tentativa no mesmo `poll`; `OK`/`ERROR` não reiniciam o prazo; ID
  expirado é ignorado; `camera_plate` devolve `-ENOENT` até haver leitura e é
  limpa no `start`. 20 testes ztest + FFF em `tests/unit/camera/`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei as três decisões do
  plano: enviar no `poll` (o prazo começa no tempo virtual do envio, sem
  `k_uptime_get()` dentro do serviço), ler os bytes antes de conferir o prazo
  (uma resposta que chega no mesmo `poll` ainda vale) e limpar a placa no
  `start`. Os 2 branches dos `__ASSERT` ficaram **sem** `LCOV_EXCL` (mesmo
  padrão do `lanes.c`).
- **Como verifiquei:** suítes passam; `camera.c` 100% de linhas e 92,86% de
  branches; total 96,85% / 86,81%; `static-check.py` com 0 achados. Nenhum
  cenário passa por ela antes da integração do `main.c`.
- **O que aprendi / o que ainda preciso entender:** por que cada tentativa usa
  um ID novo (a câmera devolve o resultado antigo para ID repetido); por que
  um byte atrasado de uma tentativa anterior não pode ser aceito; se o
  polling da UART perde bytes no Renode (ainda não confirmado).

### 2026-11-21 — Etapa 3: services/distance

- **Aluno:** Plácido Cordeiro
- **Requisito(s):** REQ-10, REQ-09 (falha do ADC trava até o reset)
- **O que pedi:** implementar `services/distance` (ADC4 alternando os canais
  17/18, uma conversão por vez, conclusão em menos de 100 ms, `chan_distance`
  no zbus, falha trava e posta `EV_ADC_FAULT`), planejado por um Opus e
  implementado por um Sonnet.
- **O que a IA propôs:** `distance.c` com estados IDLE/PENDING/LATCHED, um
  `k_work_delayable` de 5 ms, e `distance_adc_get()` em `lane_hw`; conclusão
  observada com 100 ms ou mais conta como falha e não publica; após a falha
  nunca chama o `adc4` de novo; falha posta `EV_ADC_FAULT` nas duas faixas
  depois de travar. 16 testes.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei dividir o serviço em
  `distance_init`/`distance_start`/`distance_poll` (muda o contrato: o `main.c`
  chama `distance_start()`; o teste chama `poll` direto com tempo virtual
  controlado). Aceitei que falha no `init` trave e devolva o erro sem postar
  evento (o `main` decide). Não fiz o teste sobre o ADC real no Renode. Não
  inventei valor inicial para o canal (0 significa carro no portão; o
  `presence` só reage a publicações).
- **Como verifiquei:** suítes passam; `distance.c` 100% de linhas e branches;
  total 97,17% / 87,47%; `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** por que o prazo de 100 ms
  aqui é o oposto do da câmera (conclusão no prazo ou depois é falha), e por
  que depois de travar não se rearma nada; como o driver STM32 real se
  comporta no Renode (só passa pelo ADC quando o `presence` estiver ligado).

### 2026-11-21 — Etapa 4: services/screen

- **Aluno:** Plácido Cordeiro
- **Requisito(s):** REQ-12
- **O que pedi:** implementar `services/screen` (`screen_init`,
  `screen_show_plate`, `screen_show_amount`) sobre `hw/st7789` e `text`,
  planejado por um Opus e implementado por um Sonnet.
- **O que a IA propôs:** `screen.c` com placa e valor em escala 3 e
  `SIMULATED PAYMENT` e `NOT PIX` em escala 2, todos centralizados; limpa só a
  área de cobrança (y 40..167); entrada inválida ou larga demais devolve
  `-EINVAL` antes de qualquer SPI; `payment_display_get()` em `lane_hw` e
  `CONFIG_SPI=y` na suíte. 17 testes.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei limpar só a área de
  cobrança (a tela inteira custaria 2,5x mais tempo bloqueado sem ganho).
  Aceitei que no caminho `-ERANGE` a máquina **não** chame
  `screen_show_amount` (nota no `para-kaua.md`).
- **Como verifiquei:** suítes passam; `screen.c` 100% de linhas e branches;
  total 97,35% / 88,15%; `static-check.py` com 0 achados. Os pixels só serão
  vistos num cenário quando o `main.c` chamar o serviço.
- **O que aprendi / o que ainda preciso entender:** como a janela de pixels e
  o RAMWR do ST7789 funcionam (linhas em sequência dentro de uma janela); por
  que o texto é desenhado linha a linha com um buffer estático.

### 2026-11-21 — Etapa 5: services/payment

- **Aluno:** Plácido Cordeiro
- **Requisito(s):** REQ-05, REQ-11, REQ-13
- **O que pedi:** implementar `services/payment` (UUID por contador, 3 envios
  de 1000 ms, só `PAID` exato, CANCEL no fim), planejado por um Opus e
  implementado por um Sonnet.
- **O que a IA propôs:** `payment.c` com estados IDLE/SEND/WAITING; o UUID e o
  frame `P1 CHARGE` são formados uma vez em `payment_start` e reusados nos
  reenvios e no CANCEL; `PAID` e `CANCELLED` só valem com o UUID e o valor da
  cobrança atual; no 3º prazo invalida a cobrança, envia CANCEL e só então
  posta `EV_PAYMENT_FAILED`; `payment_cancel` não posta evento. 23 testes.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei: `CANCELLED` atual
  posta `EV_PAYMENT_FAILED` sem reenviar CANCEL; `P1 ERROR` é ignorado e o
  prazo resolve; `payment_cancel` antes do 1º envio não manda CANCEL (o
  terminal nunca viu o UUID); o contador recomeça em 1 após reset.
- **Como verifiquei:** suítes passam; `payment.c` 100% de linhas e 97,06% de
  branches (falta só o `__ASSERT` do CANCEL); total 97,61% / 88,84%;
  `static-check.py` com 0 achados. Os cenários `payment-*` só passam depois da
  integração no `main.c`.
- **O que aprendi / o que ainda preciso entender:** por que a ordem em `fail()`
  é invalidar, enviar CANCEL e depois publicar o evento; por que um `PAID`
  com outro valor ou UUID antigo nunca abre a cancela; uma linha `PAID` (~60 B)
  leva 2 `poll`s de 32 bytes, então o `poll` precisa rodar a cada ~1 ms.

### 2026-11-21 — Estados de partida e assistência (fsm/states)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-01, REQ-09
- **O que pedi:** planejar o PR da faixa de entrada (M1 + M2) em passos
  curtos e implementar o passo 1, com a skill `clean-c-zephyr`.
- **O que a IA propôs:** `fsm/states.{h,c}` com os estados INIT, IDLE,
  ASSIST e ASSIST_LOCKED, a tabela da entrada (`EV_BOOT` só leva a IDLE com
  a cancela fechada; conflito de fins de curso e falha do ADC travam a
  assistência a partir de qualquer estado) e as ações `show_stop` e
  `request_assistance` (motor parado antes dos sinais). Suíte nova
  `tests/fsm/`, separada de `tests/unit` porque lá os serviços são
  compilados de verdade e os falsos do FFF colidiriam.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei os dois tipos de
  assistência (rearmável com a cancela fechada e travada até o reset).
- **Como verifiquei:** `firmware-coverage.py` com 7 testes novos passando
  (um teste errado meu foi corrigido: a guarda aparece antes no histórico de
  chamadas do FFF); `states.c` com 100% das linhas e sem branches; total de
  97,65% de linhas e 88,84% de branches (os branches não mudaram, a falta
  é anterior). `static-check.py` com 0 achados. Nenhum cenário muda ainda,
  porque o `main.c` só chama a máquina no passo 2.
- **O que aprendi / o que ainda preciso entender:**
  - Há dois tipos de assistência. A de cancela fechada (ASSIST) rearma
    quando o carro sai com o feixe livre. A de falha de posição, fins de
    curso contraditórios ou ADC (ASSIST_LOCKED) só sai com o reset.
  - A partida nunca fecha a cancela sozinha. `EV_BOOT` só leva a IDLE se
    `barrier_is_closed()`, e com os dois fins de curso ativos essa guarda
    dá falso. Por isso o conflito trava mesmo que o evento chegue antes do
    `EV_BOOT`.
  - O buzzer toca uma vez só porque a ação de entrada só roda quando há
    transição, e a linha do ASSIST_LOCKED na tabela é vazia.
  - A macro `LATCHING_FAULTS` repete as falhas que travam em todas as
    linhas, para que um estado novo não as esqueça.
  - Uma suíte de testes separada (`tests/fsm`) é necessária porque os
    falsos do FFF colidiriam com os serviços reais de `tests/unit`. No
    histórico de chamadas do FFF, a guarda aparece antes das ações.

### 2026-11-21 — Partida e thread da entrada (fsm/runner, main.c), marco M1

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-01, REQ-04 (uma faixa não segura a outra), REQ-09
- **O que pedi:** a thread da entrada e o `main.c`; se a inicialização de uma
  faixa falhar, tentar a assistência.
- **O que a IA propôs:** `fsm/runner.{h,c}`:
  - `runner_init()` configura o equipamento das duas faixas; uma faixa que
    falha recebe a assistência possível (motor parado, vermelho, buzzer) e
    não recebe `EV_BOOT`;
  - `runner_start()` cria a thread da entrada (prioridade 5);
  - `runner_step()` é uma volta da thread: um evento (espera de até 1 ms)
    e depois `camera_poll`.

  A saída só segura as saídas seguras até ganhar as regras dela (M3).
  O `main.c` só chama as duas funções, sem nenhum branch. Os falsos da suíte
  `tests/fsm` foram para `fakes/service_fakes.{h,c}`, porque cada falso só
  pode ser definido uma vez por binário.
- **O que aceitei, mudei ou rejeitei, e por quê:** escolhi a assistência em
  caso de falha de inicialização, em vez de só imprimir o erro.
- **Como verifiquei:** os cenários `partial-startup` e `contradictory-limits`
  passam. `firmware-coverage.py`: todas as suítes passam (9 testes novos);
  `runner.c`, `states.c` e `main.c` com 100% de linhas e de branches; total
  de 99,87% de linhas e 91,16% de branches (os 39 branches que faltam são de
  módulos anteriores, 17 deles no `st7789.c`). `static-check.py` com 0
  achados.
- **O que aprendi / o que ainda preciso entender:**
  - A partida é dividida em `runner_init()` e `runner_start()` para que os
    caminhos de erro sejam testados com o FFF sem criar uma thread de
    verdade dentro do binário de teste. A thread chamaria os falsos durante
    os outros testes.
  - Uma faixa que falha na inicialização fica em assistência e não recebe
    `EV_BOOT`. A outra faixa continua (REQ-04).
  - O `main.c` não tem branches: um `if` de erro nele nunca seria executado
    por um cenário e baixaria a cobertura de branches.
  - A thread espera no máximo 1 ms por um evento para que `camera_poll`
    rode cerca de uma vez por milissegundo, porque a câmera manda pedaços
    de 8 bytes a cada 10 ms.
  - `EV_BOOT` pode ser postado antes de a thread existir, porque a fila é
    estática e o evento espera nela.

### 2026-11-21 — Prazos no motor da máquina (fsm/lane, fsm/runner)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-06 (2500 ms para abrir e fechar), REQ-07 (10000 ms
  de passagem); só o mecanismo, porque os estados com prazo entram nos
  próximos passos.
- **O que pedi:** prazos por estado no motor genérico, com a skill
  `clean-c-zephyr`.
- **O que a IA propôs:**
  - `lane_def.deadline_ms[]`, um valor por estado: `LANE_DEADLINE_NONE`, um
    orçamento em ms ou `LANE_DEADLINE_KEEP` (mantém o prazo que estava
    correndo);
  - `enter()` arma o prazo antes da ação de entrada;
  - `lane_deadline_expired()` avisa uma vez só e desarma;
  - `runner_step()` despacha `EV_TIMEOUT` direto, sem passar pela fila.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. A tabela de
  prazos da entrada não foi criada agora (o campo `NULL` quer dizer "sem
  prazos"), para não ter uma tabela só de zeros; ela entra com o OPENING.
- **Como verifiquei:** `firmware-coverage.py`: todas as suítes passam (7
  testes novos em `tests/unit/fsm`, 1 em `tests/fsm`); `lane.c` e `runner.c`
  com 100% de linhas e de branches; total de 99,87% de linhas e 91,35% de
  branches. `partial-startup` e `contradictory-limits` continuam passando.
  `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:**
  - Cada estado declara o seu prazo numa tabela, como as transições. Mudar
    um prazo (pedido típico da sabatina) é mudar um número nessa tabela.
  - O `LANE_DEADLINE_KEEP` deixa um prazo cobrir vários estados. O PASSING
    mantém os 10000 ms do OPEN; se reiniciasse quando o feixe sobe, o
    `obstruction-before-close` reprovaria (assistência no tick ~1630, depois
    do 1600).
  - O `EV_TIMEOUT` é aplicado na hora, sem passar pela fila, para nunca
    chegar a um estado que já mudou.
  - O prazo é conferido a cada volta (cerca de 1 ms), dentro da tolerância
    de 10 ms da avaliação.
  - Os testes não dormem: limitam o instante em que o prazo foi armado pelo
    relógio lido antes e depois da transição, então não ficam instáveis.

### 2026-11-21 — Presença: distância com histerese e feixe (services/presence)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-02, REQ-03, REQ-07 (feixe), REQ-10 (falha do ADC na
  partida)
- **O que pedi:** o serviço de presença, com a skill `clean-c-zephyr`.
- **O que a IA propôs:**
  - `services/presence.{h,c}`: um listener do zbus em `chan_distance` com
    histerese (`ARRIVED_BELOW_CODE 683`, `LEFT_ABOVE_CODE 1024`), estado
    inicial desconhecido e `atomic_t` entre o work do `distance` e a thread
    da faixa;
  - o feixe por `hw/sensor`, que posta `EV_BEAM_ON` e `EV_BEAM_OFF` na
    interrupção;
  - as consultas `presence_car_near`, `presence_car_far` e
    `presence_beam_active`;
  - no `runner`: `presence_init` por faixa, `distance_init` uma vez (se
    falhar, as duas faixas ficam em assistência, sem `EV_BOOT`) e
    `distance_start` no `runner_start`, depois do `EV_BOOT`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. Perguntei por que
  o estado começa em desconhecido (resposta abaixo).
- **Como verifiquei:** `firmware-coverage.py`: todas as suítes passam (15
  testes novos do `presence`, 5 novos do `runner`); `presence.c` e
  `runner.c` com 100% de linhas e de branches; total de 99,88% de linhas e
  91,89% de branches. `partial-startup` e `contradictory-limits` continuam
  passando. `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:**
  - Histerese: abaixo de 683 é chegada, acima de 1024 é saída, e a faixa do
    meio não muda nada. Por isso o ruído de ±64 em torno de 682 gera uma
    chegada só (`noisy-threshold`), e um carro parado também
    (`stationary`).
  - O estado começa em desconhecido porque, antes da primeira amostra,
    `presence_car_far()` não pode dizer "sim": as guardas de passagem e de
    rearme dependem dessa resposta, e uma transição que depende da
    distância só vale com uma leitura de verdade. O valor inicial 0 do canal
    nunca é usado.
  - O estado muda antes de o evento ser postado, para que a máquina, ao
    receber `EV_CAR_LEFT`, já veja `presence_car_far()` verdadeiro.
  - A falha do ADC compartilhado é uma falha das duas faixas. A partida
    configura as faixas antes do ADC, porque a assistência precisa dos
    sinais configurados.
  - Em C, a ordem em que as expressões de uma lista de inicialização são
    avaliadas não é garantida. Por isso a configuração das faixas é feita
    num `for`, e não dentro do inicializador do vetor.

### 2026-11-21 — Tabela de sessões (services/sessions)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-04, REQ-07 (sessão só na passagem completa), REQ-13
- **O que pedi:** a tabela de vagas, com a skill `clean-c-zephyr`.
- **O que a IA propôs:**
  - `services/sessions.{h,c}` conforme a seção 4.4 do contrato:
    `sessions_can_enter`, `sessions_create` (`-EEXIST`, `-ENOSPC`),
    `sessions_entry_time` e `sessions_remove` (`-ENOENT`), mais
    `sessions_clear`;
  - a tabela tem `CONFIG_PARKING_CAPACITY` posições e um `k_mutex`. Cada
    função pública trava, chama um helper `*_locked` e destrava;
  - `runner_init` chama `sessions_clear()`.
- **O que aceitei, mudei ou rejeitei, e por quê:**
  - Escolhi o `k_mutex` no lugar do `chan_sessions` por zbus do diagrama
    06: são duas threads que só consultam quando precisam, e ninguém
    precisa ser avisado de mudanças na tabela.
  - Sem `__ASSERT` para placa `NULL`: o contrato no header diz que a placa
    nunca é `NULL`, e um assert criaria um branch que nada exercita.
- **Como verifiquei:**
  - `firmware-coverage.py`: todas as suítes passam (11 testes novos);
    `sessions.c` com 100% de linhas e de branches; total de 99,89% de linhas
    e 92,25% de branches. `static-check.py` com 0 achados.
  - Na primeira rodada, dois testes falharam por erro no próprio teste: as
    placas de preenchimento (`ABC1D20`..`ABC1D23`) incluíam a placa
    testada, e o módulo respondeu `-EEXIST`, o que está certo. Troquei o
    prefixo das placas de preenchimento.
  - Lição: uma suíte com um teste reprovado não conta nada na cobertura (o
    total caiu para 52% nessa rodada).
- **O que aprendi / o que ainda preciso entender:**
  - A placa é consultada quando é lida (`sessions_can_enter`) e a sessão só
    é criada quando a passagem termina (`sessions_create`). Isso é seguro
    porque só a entrada cria sessões: a saída só libera vagas.
  - `sessions_create` confere de novo e devolve erro; um módulo não depende
    de ser bem chamado para ficar coerente.
  - O mutex protege só a busca e a escrita na tabela (decide dentro da
    trava, age fora), então nenhuma faixa fica esperando a outra.
  - O instante gravado é o início da tarifa (M3), e o reset começa com o
    lote vazio.

### 2026-11-21 — Identificação e abertura da entrada (fsm/states)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-01 (IDLE sem fim de curso inferior), REQ-02,
  REQ-03, REQ-04, REQ-06 (prazo de abertura), REQ-09 (rearme)
- **O que pedi:** o estado IDENTIFYING e o rearme da assistência. Aceitei a
  proposta de trazer também o OPENING, para que a placa aceita tenha um
  destino e para que `jam` e `missing-upper` já passem.
- **O que a IA propôs:**
  - IDLE → IDENTIFYING na chegada, se a cancela está fechada (senão
    ASSIST_LOCKED);
  - IDENTIFYING → OPENING se `plate_may_enter` (placa lida e
    `sessions_can_enter`), senão ASSIST; `EV_CAPTURE_FAILED` → ASSIST; o carro
    saindo → IDLE;
  - OPENING com prazo de 2500 ms (`BARRIER_MOTION_BUDGET_MS`) e
    `EV_TIMEOUT` → ASSIST_LOCKED;
  - ASSIST rearma com `EV_CAR_LEFT` se o feixe está livre, ou `EV_BEAM_OFF`
    se o carro está longe;
  - `camera_abandon` nas ações de entrada de IDLE e das assistências, porque
    o motor da máquina não tem ação de saída.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. Uma mudança em
  relação ao resumo: o OPENING não guarda a placa. A câmera a mantém até o
  próximo `camera_start`, que só acontece depois do IDLE, então o fechamento
  (passo 8) pode lê-la de novo.
- **Como verifiquei:**
  - Cenários que passam agora: `low-confidence`, `camera-timeout`, `jam`
    (G3), `missing-upper` e `reset-capture`, além de `partial-startup` e
    `contradictory-limits` (7 de 19). `payment-stale` termina em `error`
    (um comando agendado do cenário falha porque a passagem ainda não
    existe), não em aprovação.
  - `firmware-coverage.py`: todas as suítes passam (14 testes novos em
    `tests/fsm`); `states.c` com 100% de linhas e de branches; total de
    99,89% de linhas e 92,28% de branches. `static-check.py` com 0 achados.
  - Um teste falhou na primeira rodada por erro do próprio teste: o FFF
    guarda só o ponteiro do argumento, e o ponteiro passado a
    `sessions_can_enter` aponta para um buffer local da guarda, que já não
    existe quando o teste o lê. Corrigi com um falso que copia a placa no
    momento da chamada.
- **O que aprendi / o que ainda preciso entender:**
  - O rearme exige duas condições ao mesmo tempo (carro longe e feixe
    livre). Qualquer uma pode ser a última, então as duas bordas viram
    eventos, e cada uma confere a outra condição pelo nível atual, numa
    guarda sem `otherwise`.
  - Sem ação de saída, a limpeza vai na ação de entrada dos estados de
    destino: todo caminho para fora do IDENTIFYING sem resultado passa por
    IDLE ou por uma assistência, e os dois chamam `camera_abandon`.
  - Prazo de abertura vencido trava a assistência, porque o braço pode ter
    parado no meio (`jam` em 0°, `missing-upper` em 90°). Só a assistência
    com a cancela fechada rearma.
  - Placa recusada (lotado ou já estacionada) leva à assistência que
    rearma: a cancela continua fechada.
  - O FFF guarda ponteiros, não o conteúdo: para verificar uma string
    passada como argumento, é preciso copiá-la num falso próprio.

### 2026-11-21 — Cancela aberta e passagem (fsm/states)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-06 (verde só no fim de curso superior com o motor
  desligado), REQ-07 (prazo de passagem de 10000 ms)
- **O que pedi:** os estados OPEN e PASSING, com a skill `clean-c-zephyr`.
- **O que a IA propôs:**
  - OPENING → OPEN com `EV_UPPER_REACHED`; a ação de entrada é
    `let_car_through` (`signals_go`);
  - OPEN → PASSING com `EV_BEAM_ON`;
  - os dois estados levam a ASSIST_LOCKED com `EV_TIMEOUT`;
  - prazos: OPEN = `PASSAGE_BUDGET_MS` (10000) e PASSING =
    `LANE_DEADLINE_KEEP`.

  A conclusão da passagem (feixe livre e carro longe) e o CLOSING ficam para
  o passo 8.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei.
- **Como verifiquei:**
  - Passam agora `confidence-retry`, `stationary`, `noisy-threshold` e
    `obstruction-before-close`: 11 de 19 no total, com `jam` e
    `contradictory-limits` do G3. O `obstruction-before-close` confirma o
    KEEP: o feixe fica preso desde o tick 630, e a assistência precisa sair
    antes do tick 1600.
  - `firmware-coverage.py`: todas as suítes passam (6 testes novos);
    `states.c` com 100% de linhas e de branches; total de 99,89% de linhas e
    92,28% de branches. `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:**
  - O verde nunca acende com o motor ligado porque o `barrier` desliga o
    motor antes de postar `EV_UPPER_REACHED`, e só esse evento leva ao
    OPEN. Um teste confere que nenhum outro evento acende o verde a partir
    do OPENING.
  - O prazo de passagem conta desde a abertura completa, e o PASSING mantém
    esse prazo. Um carro parado (`stationary`) fica com o verde até esse
    prazo vencer.
  - Prazo de passagem vencido trava a assistência, porque a cancela está
    aberta, fora da posição fechada.

### 2026-11-21 — Conclusão da passagem e fechamento (fsm/states), marco M2

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-06 (prazo de fechamento), REQ-07 (sessão só na
  passagem completa), REQ-08 (fechamento e obstrução)
- **O que pedi:** o fechamento, com um resumo curto antes (objetivo,
  arquivos, o que muda em cada um).
- **O que a IA propôs:**
  - PASSING → CLOSING pela guarda `passage_complete` (feixe livre e carro
    longe), testada nas duas bordas (`EV_BEAM_OFF`, `EV_CAR_LEFT`);
  - a ação `close_after_passage` cria a sessão (placa ainda na câmera,
    instante `k_uptime_get()`), acende o vermelho e só então fecha;
  - CLOSING: prazo de 2500 ms. `EV_LOWER_REACHED` leva a IDENTIFYING se
    `presence_car_near` (próximo carro já no portão), senão a IDLE.
    `EV_BEAM_ON` e `EV_TIMEOUT` levam a ASSIST_LOCKED.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei.
- **Como verifiquei:**
  - Passam agora `missing-lower` e `obstruction-during-close`: **13 de 19,
    incluindo os 4 do G3** (`obstruction-during-close`, `jam`,
    `contradictory-limits`, `missing-lower`). Os 6 que faltam são de saída e
    pagamento (M3).
  - `firmware-coverage.py`: todas as suítes passam (12 testes novos);
    `states.c` com 100% de linhas e de branches; total de 99,89% de linhas e
    92,37% de branches. `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:**
  - A sessão nasce na passagem completa, não na leitura da placa. A placa é
    lida de novo da câmera, que a mantém até a próxima captura.
  - O vermelho acende antes de o motor começar a fechar, e a guarda já
    garantiu o feixe livre.
  - Obstrução durante o fechamento: o feixe gera interrupção, o evento vai
    para a fila, a thread da faixa acorda na hora e a assistência para o
    motor, bem dentro dos 10 ms. Como o estado é travado, o motor não
    reverte nem religa quando o feixe libera.
  - Um carro que chega durante o fechamento tem o `EV_CAR_ARRIVED`
    ignorado, então a cancela fechada consulta o nível
    (`presence_car_near`) para não deixá-lo esperando para sempre.

### 2026-11-21 — Cobertura de branches de 92% para 95% (testes, sem exclusões)

- **Aluno:** Kauã Lessa
- **Requisito(s):** métrica de cobertura (desafio, "Pontos")
- **O que pedi:** um plano para subir os branches e a execução dos grupos 1
  a 3 (validações dos protocolos, asserts, checagens mortas).
- **O que a IA propôs:**
  - **Protocolos:** testes com uma placa errada em cada posição
    (`wire_is_plate`), um `+PLATE` sem a vírgula depois da placa e linhas
    P1 com o número certo de campos mas o verbo errado (`P1 FAILED ...`,
    `P1 REPORT ...`).
  - **Asserts:** `CONFIG_ZTEST_ASSERT_HOOK` em `tests/unit` e `tests/hw`.
    Testes chamam `lanes_get`, `lane_hw_get` e `camera_start` com uma faixa
    inválida, e `sensor_init` sem handler; o assert esperado encerra a
    thread do teste, e chegar ao fim do teste é falha.
  - **Checagem morta:** tirei o `n < 0 ||` depois do `snprintf` em
    `camera_proto.c`, `p1_proto.c` e `text.c` (código do Plácido). Um `n`
    negativo convertido para `size_t` vira um número enorme, então a
    comparação `>= size` já devolve `-ENOSPC`. Um comentário explica.
  - **Extra, para passar de 95%:** um dispositivo de teste cujo init falha
    (`DEVICE_DEFINE` em `tests/hw/errors`) faz `indicator`, `sensor`,
    `uart_line` e `adc4` devolverem `-ENODEV`. Também um canal do ADC fora
    da faixa do emulador, que o driver recusa na hora.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei; nenhuma
  exclusão `LCOV_EXCL`. O Plácido precisa revisar a mudança nos arquivos
  dele.
- **Como verifiquei:** `firmware-coverage.py`: todas as suítes passam;
  **95,64% de branches** (483/505) e 99,89% de linhas; os 13 cenários
  continuam passando. `static-check.py` com 0 achados.
- **O que ainda não está coberto (22 branches) e por quê:**
  - `st7789.c` (17): a cadeia de erros da inicialização SPI; precisaria de
    um driver SPI falso. É código do Plácido, a combinar com ele.
  - `motor.c` e `sensor.c` (4): erros de configuração de um pino num
    controlador pronto, que o `gpio-emul` nunca devolve.
  - `adc4.c:69` (1): amostra acima de 4095. O emulador e o modelo do
    Renode limitam a 4095; é uma defesa contra um driver com defeito.
  - `camera.c:54` e `payment.c:57` (2): `__ASSERT(len > 0)` depois de
    formatar um comando num buffer interno que sempre cabe; não há entrada
    pela API que o faça falhar.
- **O que aprendi / o que ainda preciso entender:**
  - Um `__ASSERT` gera um branch, e o lado da falha pode ser testado com o
    hook do ztest em vez de ser excluído.
  - O dispositivo "não pronto" se produz de verdade: um dispositivo cujo
    init falha é marcado pelo kernel como não pronto, sem driver falso
    para cada API.
  - Uma checagem redundante (`n < 0` antes de `(size_t)n >= size`) é um
    branch que nada alcança: removê-la, com o motivo escrito no código, é
    melhor do que escondê-la com uma exclusão.

### 2026-11-21 — Tarifa da saída (services/tariff)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-12 (tarifa), começo do M3
- **O que pedi:** o plano do M3 em passos curtos e o passo 1, a tarifa, com
  a skill `clean-c-zephyr`.
- **O que a IA propôs:** `services/tariff.{h,c}`:
  - `tariff_cents(parked_ms, &cents)`: unidades = max(1, ⌈ms /
    `CONFIG_PARKING_TARIFF_UNIT_MS`⌉), valor = `200 + (unidades − 1) ×
    100` centavos;
  - `-ERANGE` acima de `TARIFF_MAX_UNITS` (9999), sem alterar a saída;
  - o arredondamento para cima usa `ms / unit + (ms % unit != 0)`, que
    não estoura para uma estadia enorme, ao contrário de `(ms + unit − 1)
    / unit`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei.
- **Como verifiquei:**
  - 7 testes escritos em unidades de `CONFIG_PARKING_TARIFF_UNIT_MS`,
    para valer com qualquer unidade configurada: 0 ms, 1 ms, uma unidade
    exata, uma unidade + 1 ms, o exemplo da especificação (2,5 unidades
    dão 400), 9999 unidades (1.000.000), 9999 unidades + 1 ms
    (`-ERANGE`) e `INT64_MAX`.
  - `firmware-coverage.py`: todas as suítes passam; `tariff.c` com 100%
    de linhas e de branches; total de 99,89% de linhas e 95,66% de
    branches; os 13 cenários continuam passando. `static-check.py` com 0
    achados.
- **O que aprendi / o que ainda preciso entender:**
  - Uma unidade começada conta inteira: 1 ms já custa 200, e uma unidade
    + 1 ms custa 300.
  - 9999 unidades dão exatamente 1.000.000 centavos, o máximo que o
    terminal aceita. Por isso o limite da tarifa e o do terminal batem.
  - `(ms + unit − 1) / unit` pode estourar o `int64_t`; dividir e somar 1
    quando sobra resto dá o mesmo resultado sem esse risco.

### 2026-11-21 — Regras e thread da saída até a identificação (fsm/states, fsm/runner)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-04 (só placa com sessão sai), REQ-09 (rearme),
  REQ-04 (uma faixa não segura a outra)
- **O que pedi:** a saída com máquina e thread próprias, identificando
  carros com sessão, ainda sem cobrar.
- **O que a IA propôs:**
  - `states_exit_def()`: INIT, IDLE, IDENTIFYING (guarda
    `plate_has_session`), CHARGING (por enquanto só as falhas), ASSIST e
    ASSIST_LOCKED. As linhas iguais nas duas faixas viraram macros
    (`ROW_INIT`, `ROW_IDLE`, `ROW_ASSIST`).
  - A assistência da saída chama `payment_cancel()` antes do buzzer.
  - **Rearme na entrada do ASSIST, nas duas faixas:** se o carro já está
    longe e o feixe livre, a faixa posta `EV_CAR_LEFT` para si mesma, porque
    a borda que rearmaria já passou.
  - **`runner`:** uma tabela `setups[]` por faixa (regras, `extra_init` =
    `payment_init`, `blocking_init` = `screen_init`, `extra_poll` =
    `payment_poll`, prioridade); thread da saída com prioridade 6.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei.
- **Problema encontrado e corrigido:**
  - **Sintoma:** com o `screen_init` chamado pela `main`, 11 dos 13
    cenários de entrada passaram a reprovar.
  - **Causa:** a `main` tem prioridade 0, mais alta que a da entrada (5), e
    o desenho inicial do display é um envio longo por SPI que ocupa a CPU,
    então a entrada ficava sem rodar.
  - **Correção:** o init lento da saída roda no começo da própria thread da
    saída, com prioridade 6. Se falhar, a saída fica em assistência e a
    thread termina.
  - **Como achei:** experimentos ligando e desligando cada mudança, e a
    leitura do console de um build de cobertura com `printk` temporários
    (já removidos).
- **Segundo problema, anterior a este passo (corrigido):**
  - **Engano da IA:** eu (IA) disse que o `lab.py --headless` dava "falso
    negativo". Estava errado, e o Kauã corrigiu: o `lab.py` é ferramenta do
    curso e usa o mesmo `assess.py` do `firmware-coverage.py`. A diferença é
    o build: o do `lab.py` é otimizado e rápido; o de cobertura (gcov, sem
    otimização) é lento.
  - **Causa:** no build rápido, o `EV_BOOT` era decidido nos primeiros
    milissegundos, antes de o modelo aplicar o nível do fim de curso
    inferior no pino (as entradas são atualizadas a cada tick de 10 ms).
    `barrier_is_closed()` lia falso, e a entrada ia para ASSIST_LOCKED no
    tick 0. O build de cobertura escondia o problema; ele já existia no
    branch `feat/entry-lane`.
  - **Correção:** `INPUT_SETTLE_MS` (20 ms, dois ticks) no `runner_init()`,
    com as saídas já no estado seguro, antes de postar o `EV_BOOT`. Um teste
    confere a espera.
- **Como verifiquei:** os 19 cenários **nos dois builds**:
  - build normal (`lab.py --prepare-only` e `assess.py` por cenário):
    13/19;
  - `firmware-coverage.py`: 13/19, todas as suítes passam (cerca de 17
    testes novos em `tests/fsm`), `states.c` e `runner.c` com 100% de
    linhas e de branches, total de 99,90% de linhas e 95,84% de branches;
  - os 6 que faltam são os da cobrança. `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:**
  - Prioridade importa: um trabalho longo e sem pausas numa thread de
    prioridade alta segura as outras, mesmo que o código delas esteja certo.
    O init lento fica na thread menos urgente.
  - A assistência que rearma precisa olhar a situação na hora em que é
    acionada, e não só esperar bordas: o carro que desiste no pagamento já
    está longe quando a faixa entra no ASSIST.
  - O cancelamento do pagamento vem antes do buzzer, como pede o protocolo:
    "invalidate the active UUID before sending CANCEL, then request
    assistance".
  - Um cenário que passa num build e reprova no outro indica um problema
    de tempo no firmware, não na ferramenta. A partir de agora, cada passo
    é verificado nos dois builds.
  - Logo depois do reset, as entradas podem ainda não mostrar o nível real;
    esperar a primeira atualização (dois ticks) antes de decidir pela
    posição da cancela é a forma segura.

### 2026-11-21 — Cobrança na saída (fsm/states, estado CHARGING)

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-05 (pagamento), REQ-12 (tarifa congelada, display,
  acima de 9999 unidades), REQ-03 (desistir durante o pagamento)
- **O que pedi:** a cobrança da saída, com a skill `clean-c-zephyr`.
- **O que a IA propôs:**
  - ação `start_charge`, na ordem `screen_show_plate` → `sessions_entry_time`
    → `tariff_cents` (em `freeze_amount`) → `screen_show_amount` →
    `payment_start`;
  - todo caso em que não dá para cobrar vira um evento na própria fila:
    `-ERANGE` posta `EV_TARIFF_OUT_OF_RANGE`, sem desenhar o valor nem
    cobrar; placa ou sessão sumindo, ou erro do `payment_start`, postam
    `EV_PAYMENT_FAILED`;
  - linha do CHARGING: `EV_PAYMENT_FAILED`, `EV_TARIFF_OUT_OF_RANGE` e
    `EV_CAR_LEFT` levam ao ASSIST, que cancela a cobrança antes do buzzer e
    rearma quando o carro sai.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei.
- **Como verifiquei:**
  - Os 19 cenários **nos dois builds**: 15/19 no normal (`lab.py
    --prepare-only` + `assess.py`) e 15/19 no `firmware-coverage.py`.
    Passaram a passar `payment-missing` e `reset-payment`; faltam os 4 que
    exigem abrir com o PAID (passo 4).
  - Todas as suítes passam (9 testes novos); `states.c` com 100% de linhas
    e de branches; total de 99,90% de linhas e 95,92% de branches.
    `static-check.py` com 0 achados.
  - Um teste antigo precisou de ajuste: entrar no CHARGING agora consulta a
    sessão duas vezes (a guarda e a cobrança).
- **O que aprendi / o que ainda preciso entender:**
  - A ordem é a da especificação: a placa aparece antes de o valor ser
    calculado, o valor aparece antes do CHARGE, e o valor é calculado uma
    vez só. Os reenvios ficam dentro do `payment` e reaproveitam o mesmo
    valor e o mesmo UUID.
  - A ação de entrada não muda de estado sozinha: quando não consegue
    cobrar, posta um evento na própria faixa, e a tabela decide o que vem
    depois. Assim todo destino fica visível na tabela.
  - Acima de 9999 unidades nada é cobrado e o valor não é desenhado; a tela
    fica só com a placa.
  - Nos testes, `SET_RETURN_SEQ` do FFF faz a primeira chamada dar certo
    (a guarda) e a segunda falhar (a cobrança).

### 2026-11-21 — Saída paga, passagem e fechamento (fsm/states), marco M3

- **Aluno:** Kauã Lessa
- **Requisito(s):** REQ-05 (só PAID exato abre), REQ-06, REQ-07 (a passagem
  remove a sessão), REQ-08, REQ-04 (as duas faixas ao mesmo tempo)
- **O que pedi:** a abertura com o PAID e a saída do carro, fechando o M3.
- **O que a IA propôs:**
  - `EV_PAID` leva do CHARGING ao OPENING;
  - as linhas OPENING, OPEN, PASSING e CLOSING viraram macros
    compartilhadas (`ROW_OPENING`...), e a tabela de prazos ficou uma só
    (`barrier_cycle_deadlines_ms`), usada pelas duas faixas;
  - o fechamento ficou `close_after_entry` (cria a sessão) e
    `close_after_exit` (remove a sessão), os dois sobre `close_barrier`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei.
- **Como verifiquei:**
  - **19 de 19 cenários nos dois builds:** normal (`lab.py
    --prepare-only` + `assess.py`, um por vez) e `firmware-coverage.py`.
  - Todas as suítes passam (7 testes novos); `states.c` com 100% de linhas
    e de branches; total de 99,90% de linhas e 95,93% de branches.
    `static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:**
  - A saída reaproveita o ciclo da cancela da entrada; só mudam a guarda da
    placa (tem sessão), o estado de cobrança e o que o fechamento faz com a
    sessão.
  - O PAID que abre a cancela é só o exato e atual: quem confere UUID e
    valor é o `payment`, e a máquina só reage ao `EV_PAID`. Um teste confere
    que nenhum outro evento abre a cancela a partir do CHARGING.
  - A sessão só sai da tabela na passagem completa: pagar e abrir não
    liberam a vaga.

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| | | |
