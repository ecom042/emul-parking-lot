# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| A — _(preencher nome)_ | | parsers AT e P1, tarifa, UUID, sessões, UART dos equipamentos, display |
| B — _(preencher nome)_ | | GPIO/EXTI, ADC4, presença, máquina de estados, integração |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| A | Hermes Agent (CLI) | deepseek-v4.1-flash-1017 | 21/11/2026 20:30 a 22/11/2026 |
| A | Hermes Agent (CLI) | (mesmo modelo) | 21/11/2026 — revisão do roadmap e da especificação técnica da dupla |
| B | Claude Code (CLI) | Claude Opus 5.5 (claude-opus-5-5) | 23/11/2026 — levantamento do estado e guia de estudo |

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

## Código de terceiros

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| `src/font.c` — tabela 5x7 | desenhada para este projeto (cada glifo conferido em arte ASCII) | Apache-2.0, do próprio projeto |
| `src/hw_uart.c` — laço `uart_fifo_read`/`fifo_fill` | padrão do driver UART interrupt-driven do Zephyr | Apache-2.0 |
