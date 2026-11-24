# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| A — _(preencher nome)_ | | parsers AT e P1, tarifa, UUID, sessões, UART dos equipamentos, display (com correções do aluno B em `at_parser.c`, `hw_uart.c` e `st7789.c`, registradas abaixo) |
| B — Ycaro Sales | _(preencher)_ | GPIO/EXTI (`hw_io.c`), ADC4 (`hw_adc.c`), presença, máquina de estados das faixas (`lane.c`), thread de controle (`controller.c`, `main.c`), configuração das faixas e as suítes `lane`, `presence`, `controller`, `hw_io`, `hw_adc*`, `hw_uart`, `st7789` e os testes novos de `parsers` |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| A | Hermes Agent (CLI) | deepseek-v4.1-flash-1017 | 21/11/2026 20:30 a 22/11/2026 |
| A | Hermes Agent (CLI) | (mesmo modelo) | 21/11/2026 — revisão do roadmap e da especificação técnica da dupla |
| B | Claude Code (CLI) | Claude Opus 5.5 (claude-opus-5-5) | 23/11/2026 — levantamento do estado e guia de estudo |
| B | Claude Code (CLI) | Claude Opus 5.5 (claude-opus-5-5) | 23/11/2026 — implementação da parte B, por fases |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-21 — Contratos, tarifa, UUID e sessões

- **Aluno:** A.
- **Requisito(s):** REQ-04 (sessões), REQ-05 (valor imutável), REQ-12 (tarifa).
- **O que pedi:** congelar `parking_limits.h` conforme a fonte de verdade da
  dupla, e implementar `tariff_cents`, `uuid_generate` e a tabela de sessões com
  suíte ztest própria.
- **O que a IA propôs:** `parking_limits.h` com as constantes acordadas;
  `tariff.c` com `(dt + unit - 1) / unit` em 64 bits, `-EINVAL` para unidade
  zero ou tempo negativo e `-ERANGE` acima de 9999 unidades; `uuid.c` com
  `k_uptime_get` **combinado com um contador**; `sessions.c` com vetor estático
  de `CONFIG_PARKING_CAPACITY` e `find()` linear.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a estrutura.
  Acrescentei `sessions_count()` porque os testes de "tabela cheia" precisam
  dele sem abrir a estrutura. Recusei sortear os 16 bytes de `sys_rand_get`
  (a versão inicial): em 22/11 o link na placa mostrou que a placa não expõe
  entropia e o UUID passou a ser tempo virtual + contador — que é o que o
  contrato pede (identificador novo por tentativa, não autenticação).
- **Como verifiquei:** 1000 UUIDs distintos com entropia constante e uptime
  parado (só o contador separando); tabela de tarifa conferida contra o
  contrato (60000→200, 60001→300, 2500 ms com unidade 1000→400, 9999
  unidades→1000000, 10000→`-ERANGE`). Na placa, as suítes `tariff`
  (100% linhas, 91,7% branch), `sessions` (100%, 97,2%) e `uuid` (100%, 100%)
  passam. Antes do ambiente existir, a mesma lógica rodou num harness de host
  (1156 verificações, 0 falhas).
- **O que aprendi / o que ainda preciso entender:** por que 9999 unidades dão
  exatamente 1 000 000 centavos e por que esse é o teto do terminal; e por que
  o teto é feito com divisão inteira em vez de `ceil()` de ponto flutuante.

### 2026-11-21 — Parsers das duas UARTs

- **Aluno:** A.
- **Requisito(s):** REQ-02 (captura), REQ-11 (UARTs robustas), REQ-05.
- **O que pedi:** dois analisadores independentes, um byte por vez, buffer de
  128 bytes com terminador, descarte até o LF e prazo de 1000 ms de montagem,
  seguindo `docs/device-protocols.md` (CR LF na câmera, LF puro no terminal).
- **O que a IA propôs:** `at_parser.c` e `p1_parser.c` com a mesma máquina de
  montagem (guardar, descartar, vencer prazo) e validação canônica estrita:
  ID decimal sem zero à esquerda, placa `[A-Z]{3}[0-9][A-Z0-9][0-9]{2}`,
  confiança 0..100, UUID minusculo 8-4-4-4-12, valor 1..1000000 e `BRL`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. Rejeitei a ideia de
  o parser aplicar o limiar de confiança 85: ele valida a faixa 0..100 e a FSM
  decide, senão duas regras de negócio ficariam no analisador. Também recusei
  uma "biblioteca comum de parsing": duplicar 20 linhas de auxiliares estáticos
  mantém os dois analisadores independentes, como o contrato pede.
- **Como verifiquei:** testes de borda no harness de host, casando com os do
  ztest: quadro bom, `OK`/`ERROR` que não autorizam, ID 0/017/1000000000,
  placa minúscula e de 8 caracteres, confiança 093/101, CR ausente, CR duplo,
  dois quadros colados, fragmentos de 8 bytes, linha de 129 bytes que descarta
  até o LF e o quadro seguinte que volta a valer, linha parcial vencida em
  1001 ms, UUID maiúsculo e hífen errado, valor com zero à esquerda, `PAID`
  sem `BRL`, `CR` na linha do terminal, dois espaços, `P1 ERROR` desconhecido.
  Na placa a suíte `parsers` passa, com 93,8% das linhas em `at_parser.c` e
  86,0% em `p1_parser.c`.
- **O que aprendi / o que ainda preciso entender:** por que o contrato exige
  "descartar até o LF" em vez de simplesmente reiniciar o buffer — por causa do
  sufixo de um quadro malformado que ainda está chegando pela UART.

### 2026-11-21 — UART dos equipamentos e display

- **Aluno:** A.
- **Requisito(s):** REQ-11 (UART), REQ-12 (display e tarifa na tela).
- **O que pedi:** RX por interrupção com `ring_buf` por porta, TX que não
  bloqueia a thread de controle, e o subconjunto do ST7789V via SPI real com
  tela de cobrança (placa, `BRL r.cc`, `SIMULATED PAYMENT`, `NOT PIX`).
- **O que a IA propôs:** `hw_uart.c` com um buffer de 256 bytes por porta,
  ISR que só copia bytes e thread que drena para o analisador; `st7789.c` com
  thread própria de prioridade baixa, fila de pedidos para preservar a ordem
  (placa antes do valor) e fonte 5x7 própria em `font.c`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a fila do display,
  porque o contrato manda congelar o preço depois de desenhar a placa e antes
  de desenhar o valor. Mudei a transmissão: em vez de `uart_tx()`, a ISR
  esvazia o FIFO com `uart_fifo_fill()`, porque `uart_tx()` pode escrever só
  parte do buffer e o resto se perderia. Rejeitei limpar a tela inteira a cada
  cobrança (153 600 bytes por SPI) — cada bloco de texto sobrescreve a própria
  área.
- **Como verifiquei:** conferência das assinaturas usadas contra os headers do
  Zephyr v4.5.0 (`uart_fifo_fill`, `uart_irq_callback_user_data_set`,
  `spi_write_dt`, `SPI_DT_SPEC_GET`, `k_msgq_init`, `k_thread_create`) e
  verificação de sintaxe com `-Wall -Wextra -Werror`. Também li
  `models/ST7789V.cs` para confirmar que ele aceita comandos e pixels em
  transações separadas (`FinishTransmission` vazio) e que exige reset por
  hardware antes do primeiro byte válido. Compila na placa sem avisos.
  **Ainda sem cobertura**: suíte não exercita driver de UART nem SPI; essa
  cobertura vem dos cenários quando a FSM estiver ligada (parte do aluno B).
- **O que aprendi / o que ainda preciso entender:** por que o modelo do display
  exige `DISPON` antes do primeiro `RAMWR`, e por que a fonte é enviada linha a
  linha em vez de guardar a tela em RAM.

### 2026-11-22 — Ambiente, build e execução na placa

- **Aluno:** A.
- **Requisito(s):** portão G1 (build); base para todos os REQ.
- **O que pedi:** instalar o laboratório conforme `docs/instalacao.md` e rodar as
  suítes de unidade e o cenário `normal`.
- **O que a IA propôs:** pacotes via `dnf` (cmake, dtc, gperf, dfu-util,
  python3.12), workspace west em `~/estacionamento`, Renode 1.17.0 portable em
  `~/renode-1.17.0`, SDK 1.0.1 via `west sdk install`.
- **O que aceitei, mudei ou rejeitei, e por quê:** a ordem do guia funcionou, com
  correções: `west sdk install` falha sem `west packages pip --install` antes
  (falta o pacote `patool`); o build do Renode no Linux é
  `1.17.0+20260907gitf1dd1b4af`, um dia depois do valor citado no PDF, e o
  próprio `scripts/lab.py` documenta que o pacote carimba a data (macOS
  20260906, Linux 20260907). **Removi `CONFIG_RING_BUFFER`**: no Zephyr 4.5 ele
  só restaura a API legada e marca o símbolo como depreciado, o que a análise
  estática conta como aviso. **Tirei o `sys_rand_get` do UUID**: a placa não tem
  nó de entropia no devicetree e o link falhava com `undefined reference to
  z_impl_sys_rand_get`. Corrigi também `zassert_str_not_equal` (não existe no
  ztest 4.5) e um caminho relativo errado no `CMakeLists.txt` da suíte
  `parsers`.
- **Como verifiquei:** `normal` com o código inicial falha exatamente como o
  guia prevê (`position_mdeg expected 90000, observed 0`), o que prova a
  instalação; build com **zero avisos**; as cinco suítes passam na placa
  (`parsers`, `sessions`, `tariff`, `uuid` e o exemplo `unit`).
- **O que aprendi / o que ainda preciso entender:** por que o `--gc-sections`
  escondia o erro de link do UUID no firmware — o símbolo só era referenciado
  quando a suíte chamava `uuid_generate`, então o firmware compilava e a suíte
  não.

### 2026-11-23 — Estado da branch e guia das tarefas restantes

- **Aluno:** B.
- **Requisito(s):** todos (planejamento); nenhum código escrito.
- **O que pedi:** saber o que já está pronto na branch `solucao`, o que os
  cenários públicos exigem, quais já passam, e um guia do que falta fazer e do
  que estudar para isso, **sem sugestão de implementação**, porque vou
  programar a máquina de estados à mão.
- **O que a IA propôs:** resumo dos módulos do aluno A (prontos e testados por
  suítes, exceto UART e display, que só compilam); explicação dos 19 cenários
  agrupados por requisito; e um guia em 13 fases (leitura, GPIO/EXTI, partida e
  assistência, movimento, ADC4, presença, câmera, passagem, sessões,
  pagamento/display/tarifa, reset, qualidade, entrega), cada uma com o que
  fazer, os cenários que a verificam e os conhecimentos de Zephyr necessários.
  O guia ficou num documento privado no claude.ai, fora do repositório.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei o guia como roteiro
  de estudo. A IA apontou um ponto a investigar: `uuid_generate` deriva o UUID
  do tempo virtual e de um contador, que recomeçam no reset (REQ-13).
- **Como verifiquei:** rodei os 19 cenários um a um com `scripts/assess.py`
  sobre o firmware atual (`build/each-01/`): **nenhum passa**, como esperado com
  o `main.c` inicial; o mais próximo é `partial-startup`/`contradictory-limits`,
  que só falham em `buzzer_count`.
- **O que aprendi / o que ainda preciso entender:** a ordem das fases pelo
  portão G3; ainda preciso entender a posse única do ADC4 assíncrono e onde a
  reação ao feixe deve ficar para cumprir 10 ms.

### 2026-11-23 — Guia conferido com a fonte de verdade e o roadmap B

- **Aluno:** B.
- **Requisito(s):** todos (planejamento); nenhum código escrito.
- **O que pedi:** atualizar o guia com a fonte de verdade da dupla e o roadmap
  B, ignorando as partes sobre agentes de IA, e decidir comigo cada divergência.
- **O que a IA propôs:** sete pontos em que a fonte de verdade contradiz o
  `docs/`: (1) o fim de curso desligaria o motor na própria ISR, mas
  `device-protocols.md` manda adiar para o contexto de work; (2) o prazo da
  câmera contaria do primeiro byte, mas conta do envio, com a resposta
  fragmentada inteira; (3) carro saindo durante a cobrança voltaria a IDLE, mas
  o contrato manda invalidar o UUID, enviar `P1 CANCEL` e pedir assistência;
  (4) o mesmo vale para o terceiro envio vencido; (5) a obstrução no
  fechamento, que estava em aberto, deixa a cancela no meio, então é falha de
  posição travada até o reset; (6) semear o ID de captura no boot não é exigido,
  porque o reset também limpa o cache da câmera; (7) o registro de IA é o portão
  G4, não vale 10 pontos. A IA também apontou que parte da interface congelada
  (`display_show_charge`, `EV_DISPLAY_DONE`, `hw_camera_send`, UART postando
  eventos) não existe no código; o real é `st7789_show_plate`/`_charge`,
  `hw_uart_send` e `hw_uart_poll_*`.
- **O que aceitei, mudei ou rejeitei, e por quê:** segui o `docs/` nos sete
  pontos, como a própria fonte de verdade manda. Incluí no guia o desenho da
  dupla (estados com WAIT_BEAM_CLEAR, divisão em arquivos, debounce de 3
  amostras), com a API corrigida para o código real. Fiquei só com a meta de
  95% de cobertura do `AGENTS.md`, não com a de 85/75. Dos extras, só o
  checklist final entrou.
- **Como verifiquei:** cada correção conferida no texto de
  `docs/device-protocols.md` (seções do controlador, da câmera e do P1) e nos
  headers `hw_uart.h`, `st7789.h`, `tariff.h` e `sessions.h`.
- **O que aprendi / o que ainda preciso entender:** que o cancelamento por
  saída do carro também gera assistência, e por que a fonte de verdade precisa
  ser corrigida nesses pontos.

### 2026-11-23 — Headers dos contratos do aluno B

- **Aluno:** B.
- **Requisito(s):** base de REQ-01 a REQ-10 (interfaces); nenhuma implementação.
- **O que pedi:** transformar os contratos do guia (interface congelada da
  fonte de verdade, já corrigida pelo `docs/` e pelo código real do aluno A) em
  arquivos, para eu implementar à mão.
- **O que a IA propôs:** só headers em `src/`, sem `.c`: `events.h` (enum de
  eventos e `struct lane_event`), `lane.h` (estados e `lane_init`/
  `lane_handle_event`), `hw.h` (motor, sinais, buzzer, feixe, fins de curso),
  `hw_adc.h` (`hw_adc_start`/`hw_adc_read`) e `presence.h`
  (`presence_update`). Diferenças em relação à fonte de verdade: sem
  `EV_DISPLAY_DONE` nem `hw_camera_send`/`hw_payment_send`/
  `display_show_charge`, que não existem no código do aluno A (usar
  `hw_uart_*` e `st7789_*`); estados com prefixo `LANE_`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. Ficaram em aberto,
  para eu decidir: os campos de `struct lane_cfg` (só declarada, porque com
  `gpio_dt_spec` ela levaria header de driver para a lógica), os campos
  restantes de `struct lane` e o significado exato de `hw_adc_start`. Os
  headers não entram no `CMakeLists.txt` até existirem os `.c`.
- **Como verifiquei:** `gcc -std=gnu11 -fsyntax-only -Wall -Wextra -Wshadow
  -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wpedantic` num arquivo que
  inclui os cinco headers: zero avisos. Nenhum cenário muda, porque nada os
  inclui ainda.
- **O que aprendi / o que ainda preciso entender:** como separar a
  configuração de hardware da faixa (aliases) da parte que a lógica precisa
  ler, sem quebrar a regra de headers de driver só em `hw_*.c`.

### 2026-11-23 — Fase 1: hardware das faixas, ADC4, presença e partida segura

- **Aluno:** B (Ycaro Sales).
- **Requisito(s):** REQ-01 (partida segura), REQ-09 (assistência e rearme),
  REQ-10 (ADC4 assíncrono), REQ-03 (presença), base de REQ-06 e REQ-08.
- **O que pedi:** terminar a atividade pelo guia, me perguntando cada decisão
  de arquitetura antes de cada fase. Nesta fase: a camada de GPIO/EXTI, o
  ADC4, a presença, a thread de controle e os estados de assistência.
- **Decisões que tomei (a IA perguntou antes de codificar):** uma única
  thread de controle de 10 ms para as duas faixas, dona única do ADC4 (em vez
  de uma thread por faixa); pinos escondidos em `hw_io.c` e `hw_adc.c`, com
  `lane_cfg` só lógica (id, porta da câmera, se cobra); falha do ADC4 trava as
  **duas** faixas, porque o conversor é um só; pulso do buzzer por
  `k_work_delayable`; a ISR do feixe registra as bordas em `atomic_t` e corta
  o motor quando a faixa está fechando.
- **O que a IA propôs:** `lane_cfg.c/h`; `hw_io.c` (saídas inativas e
  vermelho na partida; fim de curso com interrupção na subida, que agenda um
  `k_work` para desligar o motor, como manda `device-protocols.md`; feixe nas
  duas bordas); `hw_adc.c` (sequência, buffer e `k_poll_signal` estáticos,
  `adc_read_async` alternando os canais 17 e 18, `k_poll_signal_check` sem
  bloquear, prazo de 100 ms e trava); `presence.c` (histerese 683/1024 e 3
  amostras consecutivas); `controller.c` (um tick: ADC, fins de curso,
  feixe, UARTs, prazos); `main.c` com sono em instante absoluto
  (`K_TIMEOUT_ABS_MS`) para o período não escorregar; `lane.c` com a partida
  (inferior → IDLE, os dois → LOCKED, outro → ASSIST, nunca fecha sozinha),
  LOCKED para limites contraditórios, falha do ADC e erro de driver, um só
  pulso de buzzer por pedido de assistência e rearme só com cancela fechada,
  motor parado, distância recente > 1024 e feixe livre. `events.h` passou a
  levar o tempo virtual em cada evento, para a lógica ser testável sem
  relógio. Suítes novas: `presence` e `lane` (FFF para `hw.h`, UART e
  display).
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. A análise
  estática apontou um `-Wcast-qual` em `st7789.c` (do aluno A): o
  `(void *)dados` tirava o `const`. A correção tira o `const` dos parâmetros
  de `escreve`/`comando` e dos dois bytes de configuração, em vez de esconder
  o cast.
- **Como verifiquei:** `partial-startup` e `contradictory-limits` passam
  (antes nenhum passava); `jam` e `missing-upper` ainda falham, como
  esperado, porque a captura e a abertura são da fase 2. `static-check.py`:
  **0 achados**. `firmware-coverage.py` com esses dois cenários e as suítes:
  todas as suítes passam; `presence.c` 100% de linhas e branches, `lane.c` 98,8% de linhas e 91,1% de branches; total 70,1% / 58,1% (só dois cenários).
- **O que aprendi / o que ainda preciso entender:** por que o callback do
  fim de curso não desliga o motor direto e o do feixe sim (prazo de 10 ms da
  obstrução contra a regra de adiar para work); por que uma conclusão do ADC
  vista depois de 100 ms trava mesmo que o valor seja bom.

### 2026-11-23 — Fase 2: fluxo da entrada (captura, abertura, passagem, fechamento)

- **Aluno:** B (Ycaro Sales).
- **Requisito(s):** REQ-02, REQ-03, REQ-04 (entrada), REQ-06, REQ-07, REQ-08;
  portão G3.
- **O que pedi:** a identificação e o movimento da cancela na entrada, até os
  cenários do G3 passarem.
- **Decisões que tomei (a IA perguntou antes):** tirar o estado
  `WAIT_BEAM_CLEAR` do desenho da dupla, porque a passagem só completa com o
  feixe já inativo e `hw_motor_drive(fechar)` já recusa fechar com o feixe
  ativo; ID de captura por faixa, contando de 1 (cada câmera tem cache
  próprio e o reset o limpa); envio da câmera que falha conta como tentativa e
  vence pelo prazo de 1000 ms, sem caminho especial.
- **O que a IA propôs:** em `lane.c`, IDLE → IDENTIFYING quando a cancela está
  fechada e parada, a presença foi aceita com amostra recente e essa presença
  ainda não foi atendida (`served`); `+PLATE` só do ID atual, confiança ≥ 85,
  até 3 tentativas com ID novo e 1000 ms; código recente > 1024 abandona e
  invalida o ID; entrada abre só sem sessão e com vaga; OPENING acende o
  verde só com o fim de curso superior e o motor desligado (desliga ele mesmo
  se o work do EXTI ainda não rodou); prazo de 2500 ms trava (LOCKED);
  WAIT_PASSAGE exige feixe ativo → inativo, feixe livre e código recente >
  1024, cria a sessão nesse instante, acende o vermelho e fecha; prazo de
  10 s pede assistência com a cancela aberta; CLOSING trava na obstrução
  (a ISR já cortou o motor) e no prazo, e volta a IDLE no fim de curso
  inferior.
- **Bug encontrado e corrigido:** com o fluxo pronto, nenhum cenário
  capturava. O trace mostrou o buzzer das duas faixas 300 µs após o boot:
  `lane_init` lia o fim de curso inferior como baixo. O modelo explica
  (`ParkingPhysical.cs`, `Drive`): depois do reset da máquina o receptor GPIO
  só volta a ver o nível na primeira amostra de 10 ms. A partida passou a ser
  classificada no primeiro `EV_LIMITS`, e a thread espera um tick antes do
  primeiro serviço.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei.
- **Como verifiquei:** passam `jam`, `missing-lower`, `contradictory-limits`,
  `obstruction-during-close` (G3), `missing-upper`, `obstruction-before-close`,
  `stationary`, `noisy-threshold`, `confidence-retry`, `low-confidence`,
  `camera-timeout` e `partial-startup`. Falham só os 6 de pagamento e reset de
  pagamento (fase 3). `firmware-coverage.py` (19 cenários + suítes, só os que
  passam contam): total 81,1% de linhas e 68,9% de branches; `lane.c` 99,5% /
  97,0%. `static-check.py`: 0 achados. Suíte `lane` com 56 testes, cada um com
  `zassert`, incluindo os erros de driver por FFF.
- **O que aprendi / o que ainda preciso entender:** por que o modelo
  "reafirma" os sensores a cada amostra e o que isso muda na partida; por que
  o verde só pode acender com o motor já desligado.

### 2026-11-23 — Fase 3: saída com display, tarifa e pagamento; display corrigido

- **Aluno:** B (Ycaro Sales).
- **Requisito(s):** REQ-04 (saída), REQ-05, REQ-12, REQ-13.
- **O que pedi:** a cobrança da saída até os 19 cenários públicos passarem.
- **Decisões que tomei (a IA perguntou antes):** congelar a tarifa logo
  depois de enfileirar a placa no display (`st7789_show_plate` →
  `tariff_cents` → `st7789_show_charge` → `P1 CHARGE` no mesmo tick), porque a
  fila do display preserva a ordem no SPI; display que recusa o pedido ou
  tarifa acima de 9999 unidades levam à assistência **sem** cobrar; um
  `P1 CANCEL` que volta `-EBUSY` é reenviado a cada tick até sair.
- **O que a IA propôs:** em `lane.c`, saída só com sessão ativa
  (`sessions_entry_time`, que já dá o instante de entrada); UUID novo por
  tentativa de saída; até 3 envios de 1000 ms com a mesma linha; só abre com
  `PAID` do UUID atual e do valor congelado, e o UUID é consumido nesse
  momento (duplicata não reabre); `CANCELLED`, terceiro envio vencido, carro
  que sai (código recente > 1024) e qualquer falha durante a cobrança
  invalidam o UUID **antes** do `P1 CANCEL` e pedem assistência — isso ficou
  num lugar só, em `enter_assist`. A passagem na saída remove a sessão.
  `controller_init` passou a chamar `st7789_start`. O reset (REQ-13) não
  precisa de código: nada sobrevive na RAM, e o reset limpa também as caches
  da câmera e do terminal.
- **Bug encontrado no módulo do aluno A (`st7789.c`):** com os cenários
  passando, o snapshot do modelo mostrava o display `initialized: false`,
  0 pixels e 23571 erros (os cenários públicos não olham o display). Causas:
  os pinos D/C e reset nunca eram configurados como saída, e o reset é
  `GPIO_ACTIVE_LOW` no devicetree, mas o código escrevia 0 e depois 1 —
  soltava e depois prendia o reset. Correção: `gpio_pin_configure_dt` dos
  dois pinos e pulso com 1 (preso) por 10 ms e 0 (solto). Depois disso:
  `initialized: true`, 0 erros e 11760 pixels, exatamente a soma das quatro
  áreas de texto (placa, `BRL r.cc`, `SIMULATED PAYMENT`, `NOT PIX`).
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei.
- **Como verifiquei:** **19 de 19** cenários públicos passam.
  `static-check.py`: 0 achados. `firmware-coverage.py`: **95,3% de linhas e
  78,6% de branches** (`lane.c` 100% / 98,2%). Suíte `lane` com 69 testes,
  incluindo a ordem placa → valor → terminal conferida pelo histórico do FFF.
- **O que aprendi / o que ainda preciso entender:** a diferença entre
  resposta duplicada (mesmo UUID, já consumido) e antiga (outro UUID); por que
  o UUID é invalidado antes do `CANCEL`; como ler o snapshot do display nas
  evidências do cenário.

### 2026-11-23 — Fase 4: qualidade (testes de drivers, controller e parsers)

- **Aluno:** B (Ycaro Sales).
- **Requisito(s):** métricas de cobertura e análise estática; REQ-02 e
  REQ-11 (bugs nos parsers/UART).
- **O que pedi:** subir a cobertura de branches (78,6%) acima de 95% com
  testes de verdade.
- **Decisões que tomei (a IA perguntou antes):** cobrir os erros de driver
  com suítes que compilam o `hw_*.c` real com um header forçado
  (`-include shim.h`) que redireciona só as chamadas de driver do Zephyr para
  falsos do FFF — o código de produção não muda; acrescentar testes à suíte
  `parsers` do aluno A; tirar a lógica do `main.c` para `controller_run()`
  (testável) em vez de usar `LCOV_EXCL`. **Nenhuma exclusão foi usada.**
- **O que a IA propôs:** a IA rodou um workflow de agentes, cada um com uma
  suíte: `hw_io` (41 testes; ISRs chamadas pelo callback capturado, buzzer
  pelo work real), `hw_adc` + `hw_adc_submissao`/`_resultado`/`_codigo`
  (11 testes; quatro pastas porque a trava do ADC só sai com reset da
  máquina, então cada trava de conversão precisa de um boot limpo),
  `hw_uart` (21), `st7789` (26, incluindo `font.c`), `controller` (25) e
  28 testes novos em `parsers`, mais testes em `lane` e `sessions`. Um
  agente revisor adversarial conferiu o diff: nenhum teste sem verificação,
  nenhuma verificação de erro enfraquecida, nenhum shim que mude a lógica.
- **Bugs do aluno A revelados pelos testes e corrigidos:**
  `at_parser.c` exigia 21 bytes no `+PLATE`, mas o menor válido tem 20
  (`+PLATE:5,"ABC1D23",0`): captura com ID e confiança de um dígito era
  descartada e a faixa esperava o prazo em vez de tentar de novo.
  `hw_uart.c` tirava um bloco de 16 bytes do buffer e perdia os bytes que
  viessem depois de um quadro completo; agora lê um byte por vez.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei; cada correção
  num commit próprio, com teste de regressão.
- **Como verifiquei:** `firmware-coverage.py` (19 cenários + todas as
  suítes, todos passando): **99,9% de linhas e 98,1% de branches**.
  `static-check.py`: **0 achados**. Os 13 desfechos restantes estão em
  macros do Zephyr ou em combinações que o código já impede (por exemplo
  `tariff.c` 36:1); a linha descoberta de `main.c` é o `return 0` depois de
  `controller_run()`, que só volta em falha de setup.
- **O que aprendi / o que ainda preciso entender:** por que a ferramenta só
  conta um desfecho da suíte quando o build tem o mesmo número de desfechos
  na linha (e por isso o shim não pode mudar a lógica); como capturar e
  chamar uma ISR num teste.

### 2026-11-23 — Nota: trabalho autônomo até 24/11 06:00

- **Aluno:** B (Ycaro Sales).
- **O que pedi:** depois da fila de carros, pedi para a IA seguir sem me
  perguntar nada até as 06:00 de 24/11, fazendo push de cada commit
  verificado para o repositório privado da dupla. Decisões tomadas nesse
  período aparecem marcadas como "decidida pela IA sem consulta", para eu
  revisar.

### 2026-11-23 — Fila de carros e passagem depois do prazo

- **Aluno:** B (Ycaro Sales).
- **Requisito(s):** REQ-04 (faixas simultâneas, sessões), REQ-07, REQ-09.
- **O que pedi:** exercitar combinações que os cenários ocultos devem usar,
  com o modo de fila de carros da ferramenta do curso
  (`assess.py --lot SEGUNDOS --cars perfis.json`), as duas faixas ao mesmo
  tempo.
- **O que a IA propôs:** cinco baterias de perfis (carros normais,
  `retry_recovers`, pagamento `delayed`, `hesitate`; falhas `failed`,
  `no_reply`, `misread`, `cancelled`, `malformed`, `no_reply` no terminal e
  `leave_during_payment`; `linger_in_beam`; `block_closing`; e lotação com
  `--capacity 1`). Resultado: nenhuma violação de segurança nem do lot
  oracle; 37 de 38 vereditos passaram. O que falhou: um carro segurou o feixe
  além dos 10 s, o firmware pediu assistência com a cancela aberta (certo),
  o carro entrou depois e, como a passagem completou fora do prazo, não
  ganhou sessão; na saída foi recusado. O driver do curso e o lot oracle
  dos cenários contam como estacionado todo feixe ativo → livre depois do
  verde.
- **Decisão que tomei (a IA perguntou):** registrar a passagem tardia. Na
  assistência que vem do prazo da passagem (`late_passage`), a faixa segue
  acompanhando o feixe; se a passagem completar, cria (entrada) ou encerra
  (saída) a sessão, mas continua em assistência com a cancela aberta — nada
  fecha sozinho. O acompanhamento da passagem virou uma função só
  (`passage_completed`), usada em WAIT_PASSAGE e nessa assistência.
- **Como verifiquei:** as baterias repetidas passam inteiras (34 de 34
  vereditos, sem falha de segurança). `firmware-coverage.py` (19 cenários +
  todas as suítes passando): 99,9% de linhas e 98,1% de branches.
  `static-check.py`: 0 achados. Suíte `lane` com 75 testes (4 novos da
  passagem tardia). Uma execução anterior falhou por falta de espaço no
  disco (btrfs sem espaço não alocado), não por causa do código; o aluno
  liberou espaço e a execução foi repetida.
- **O que aprendi / o que ainda preciso entender:** a diferença entre o
  prazo da passagem (quando pedir ajuda) e a passagem em si (quem está no
  estacionamento).

### 2026-11-23 — Constantes sem uso em `parking_limits.h`

- **Aluno:** B (Ycaro Sales). **Decidida pela IA sem consulta** (período de
  trabalho autônomo), para eu revisar.
- **O que aconteceu:** ao montar o roteiro da sabatina, a IA notou que
  `GATE_TICK_TOL_MS` e `BEAM_STOP_MS` eram definidas e nunca usadas. Mudar
  uma delas ao vivo não teria efeito nenhum.
- **O que a IA fez:** trocou as duas por um comentário que diz onde esses
  limites são cumpridos: a tolerância de um tick é do avaliador (o prazo de
  2500 ms é conferido a cada tick em `lane.c`), e o corte em ≤ 10 ms é
  imediato na ISR do feixe (`hw_io.c`, `beam_isr`).
- **Como verifiquei:** `obstruction-during-close`, `jam` e `normal` passam;
  `static-check.py` com 0 achados. Só comentário e `#define` sem uso mudaram.

### 2026-11-24 — Achados da revisão independente

- **Aluno:** B (Ycaro Sales). **Correções decididas pela IA sem consulta**
  (período de trabalho autônomo), para eu revisar.
- **O que aconteceu:** um agente revisor recebeu o código e o contrato
  (`docs/`), sem as conclusões anteriores, e procurou violações que só um
  cenário oculto mostraria. Achou, com cenário concreto:
  1. perder o fim de curso inferior com a faixa em IDLE só parava de
     identificar, sem pedir assistência ("IDLE without a lower limit requests
     assistance"); o mesmo durante identificação ou cobrança;
  2. a amostra "carro longe" (> 1024) só era tratada no tick, depois dos
     quadros do mesmo passo: um `+PLATE` ou `PAID` que chegasse no mesmo tick
     da saída do carro autorizava a abertura;
  3. feixe ativo no exato tick em que a cancela fecha levava a LOCKED com a
     cancela fechada (a faixa morria até o reset sem posição parcial);
  4. o `P1 CHARGE` sai pela UART antes de a thread do display desenhar a placa
     e o valor (consequência da decisão de congelar a tarifa ao enfileirar a
     placa). **Não mudei:** é decisão minha, fica para eu revisar.
- **O que a IA fez:** (1) em IDLE, IDENTIFYING e CHARGING, inferior perdido →
  assistência (cobrança cancelada antes, como em todo abandono); (2) o
  abandono é avaliado já no evento de distância, que chega antes dos quadros
  do mesmo tick; (3) no CLOSING, feixe com inferior acionado e motor parado
  conclui o fechamento (IDLE) em vez de travar. Sete testes novos em `lane`.
- **Como verifiquei:** `lab.py --headless all`: 19/19. Fila de carros: 34/34
  vereditos. `static-check.py`: 0 achados. Suíte `lane` passa (81 testes),
  `lane.c` com 100% de linhas e 98,97% de branches.

### 2026-11-24 — Cobrança espera o display (ordem do contrato)

- **Aluno:** B (Ycaro Sales).
- **O que pedi:** que o `P1 CHARGE` só saia depois de a placa e o valor
  estarem desenhados, como pede o contrato ("Freeze at the first exit charge
  calculation, after rendering the recognized plate and before rendering
  amount/status and sending CHARGE"). Isso revê a minha decisão anterior de
  congelar a tarifa ao enfileirar a placa.
- **O que a IA propôs e fez:**
  - `st7789.c` (do aluno A): cada pedido ganha um número (nunca 0, por isso
    `emitidos % UINT32_MAX + 1`); a thread publica `st7789_failed()` e depois
    `st7789_done()`; desenhar devolve o erro de SPI; a inicialização (~400 ms
    de espera) passa a ser no boot, para não atrasar a cobrança.
  - `controller.c`: quando `st7789_done()` muda, entrega `EV_DISPLAY` à saída.
  - `lane.c`: estado novo `LANE_DISPLAYING`. Placa enfileirada → espera o
    `EV_DISPLAY` dela → congela a tarifa → enfileira o valor → espera →
    UUID novo e o primeiro `P1 CHARGE`. Falha do display ou prazo de
    `DISPLAY_MS` (1000 ms) → assistência sem cobrar; carro que sai → abandona
    (ainda não há cobrança a cancelar); inferior perdido → assistência.
  - Testes: `lane` (89), `controller` (33) e `st7789` (27 + 3 da fonte).
- **Bug achado pelos testes:** na volta do contador o número 0 se confundia
  com o "nenhuma falha" inicial; resolvido pulando o 0 sem criar branch.
- **Mudança de comportamento:** a tela da cobrança para no primeiro texto que
  falha (antes desenhava o status mesmo com o valor recusado). Nenhum valor
  válido chega a isso: o maior, `BRL 10000.00`, cabe na tela.
- **Como verifiquei:** `lab.py --headless all`: 19/19. Fila de carros: 34/34.
  `firmware-coverage.py`: 99,91% de linhas e 98,22% de branches, todas as
  suítes passando. `static-check.py`: 0 achados. Os 4 desfechos que faltam
  em `st7789.c` são checagens de janela que a API nunca dispara (texto vazio,
  escala 0, altura além da tela).

### 2026-11-24 — Revisão das decisões sem consulta

- **Aluno:** B (Ycaro Sales).
- **O que pedi:** revisar as decisões que a IA tomou sem me consultar.
- **O que decidi:** mantive a remoção de `GATE_TICK_TOL_MS` e
  `BEAM_STOP_MS`, a assistência ao perder o fim de curso inferior e o
  abandono avaliado já no evento de distância. Para o feixe que ativa no
  mesmo tick em que a cancela chega ao inferior, pedi a um agente de contexto
  limpo que investigasse o `docs/`: a especificação não decide o caso; o
  oráculo (`scripts/scenario/obstruction.py`) só trava com posição > 0, e o
  contrato diz "an obstruction during closing stops the motor and requests
  assistance" e "assistance at a closed barrier rearms only after distance
  >1024 with clear beam". O firmware não mede posição; o equivalente a
  "posição 0" é o fim de curso inferior acionado. **Escolhi assistência
  rearmável** nesse caso, em vez de IDLE (a escolha da IA) ou LOCKED.
- **O que a IA fez:** em `handle_event_closing`, feixe com o inferior
  acionado e o motor parado passa a `enter_assist(LANE_ASSIST)`; o teste
  confere um pulso de buzzer, motor nunca religado e o rearme só com feixe
  livre e carro longe.
- **Como verifiquei:** `lab.py --headless all`: 19/19. `firmware-coverage.py`:
  99,91% de linhas e 98,22% de branches, todas as suítes passando.
  `static-check.py`: 0 achados.

## Código de terceiros

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| `src/font.c` — tabela 5x7 | desenhada para este projeto (cada glifo conferido em arte ASCII) | Apache-2.0, do próprio projeto |
| `src/hw_uart.c` — laço `uart_fifo_read`/`fifo_fill` | padrão do driver UART interrupt-driven do Zephyr | Apache-2.0 |
