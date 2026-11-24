# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

Trabalho individual.

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| Jeyson Nascimento | 19212510 | todo o `student/` |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| Jeyson Nascimento | Claude Code (CLI) | Claude Opus 5.5 | 2026-11-18 em diante |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-18 — Análise do desafio, especificação e plano

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** todos (REQ-01 a REQ-13)
- **O que pedi:** análise do `desafio.pdf`, uma especificação, a explicação
  de cada requisito para a sabatina e um plano de tarefas.
- **O que a IA propôs:** especificação com módulos (`hw`, `barrier`, `lane`,
  `adc_sampler`, `line_parser`, `camera`, `payment`, `tariff`, `sessions`,
  `display`), laço único de 10 ms com parada do motor via EXTI → `k_work`,
  UART por interrupção; explicação dos requisitos; plano em 18 tarefas.
- **O que aceitei, mudei ou rejeitei, e por quê:** escolhi o laço único em
  vez de uma thread por faixa (sem mutex nos dados compartilhados, mais fácil
  de testar e explicar) e a UART por interrupção.
- **Como verifiquei:** leitura do enunciado e dos contratos em `docs/`.
- **O que aprendi / o que ainda preciso entender:** aprendi a ler o
  desafio como contratos (entradas, saídas e prazos de cada requisito) antes
  de escrever código, e a diferença prática entre laço único e uma thread por
  faixa: com o laço, só a work do fim de curso concorre com o `main`, e o
  resto dos dados não precisa de mutex. Preciso revisar a ordem de
  prioridade entre as threads do sistema (workqueue x `main`).

### 2026-11-18 — Instalação do laboratório

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** —
- **O que pedi:** instalar o ambiente e ligá-lo ao repositório privado.
- **O que a IA propôs:** seguiu `docs/instalacao.md`. Como o `scripts/lab.py`
  recusava o Renode 1.17.0 oficial (SHA-256 conferido; o binário reporta o
  build `20260907` e o script espera `20260906`), propôs um ajuste **só
  local** dessa checagem, fora dos commits (`git update-index --skip-worktree`).
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei o ajuste local,
  sem commit, porque o binário era o oficial. Nada fora de `student/` é
  entregue. O professor publicou a correção na v1.0.1 (ver entrada de
  2026-11-23), e o ajuste local foi descartado.
- **Como verifiquei:** cenário `normal` falhou exatamente como o guia prevê
  (`position_mdeg expected 90000, observed 0`).
- **O que aprendi / o que ainda preciso entender:** aprendi a estrutura de
  um workspace `west` (manifesto, módulos, `west update --narrow`) e que o
  `lab.py` confere a identidade do Renode antes de rodar. Aprendi a conferir
  um binário pelo SHA-256 antes de confiar nele.

### 2026-11-18 — Camada de hardware, cancela e partida segura

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-01, REQ-06, REQ-08, REQ-09 (parte)
- **O que pedi:** a tarefa T2 do plano.
- **O que a IA propôs:** `src/config.h` (constantes), `src/hw.c/.h` (GPIO,
  EXTI → `k_work`), `src/barrier.c/.h` (estados da cancela, prazo de 2500 ms,
  parada pelo fim de curso, obstrução, contraditórios; comunicação com a work
  só por `atomic_t` e `k_sched_lock` ao ligar o motor), `src/lane.c/.h`
  (partida e assistência com um único pulso de buzzer), `src/main.c` (laço de
  10 ms); suítes `tests/barrier` e `tests/lane` com `tests/common/hw_fake.c`.
  Removeu o exemplo `tests/unit`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a divisão em
  `hw` (único módulo que toca o Zephyr) e `barrier`/`lane` (lógica pura),
  porque permite testar a lógica com falsos do FFF sem o simulador. Aceitei
  parar o motor dentro da work disparada pela EXTI, e não no laço de 10 ms,
  porque o fim de curso exige reação imediata. Removi o exemplo `tests/unit`
  por não testar nada do projeto.
- **Como verifiquei:** cenários `partial-startup` e `contradictory-limits`
  passam; `firmware-coverage.py` com as suítes `unit:barrier` e `unit:lane`
  passando (parcial: 95,2% linhas, 85,3% branches).
- **O que aprendi / o que ainda preciso entender:** aprendi por que a ISR do
  GPIO só agenda uma `k_work` (não pode bloquear nem usar SPI), e como
  `atomic_t` e `k_sched_lock` evitam que a work e o laço vejam a cancela num
  estado pela metade. Aprendi a usar FFF para falsificar `hw.h` e testar a
  lógica sem o simulador. Preciso entender melhor o pior caso de latência
  entre a borda do fim de curso e o motor desligar.

### 2026-11-18 — ADC4 assíncrono e acomodação da partida

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-10, REQ-01
- **O que pedi:** a tarefa T5 do plano.
- **O que a IA propôs:** `hw_adc_start`/`hw_adc_poll` em `src/hw.c`, com
  buffer, `adc_sequence` e `k_poll_signal` estáticos, verificação por
  `k_poll_signal_check`; `src/adc_sampler.c` com um pedido por vez,
  alternância dos canais 17/18, amostra fresca consumida uma vez, prazo de
  100 ms e falha travada sem reenvio; falha do ADC leva as faixas a
  assistência travada. Suítes `tests/adc` (falsos) e `tests/hw` (drivers
  reais contra os modelos do Renode).
  Achado durante a verificação: logo após o boot (t = 278 µs) as entradas
  ainda não refletem os modelos, e a faixa de saída pedia assistência sem
  motivo. A conferência dos fins de curso passou para o primeiro passo depois
  de `STARTUP_SETTLE_MS` (20 ms), com motor desligado e vermelho aceso.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei o ADC assíncrono
  (`adc_read_async` + `k_poll_signal`) em vez do `adc_read` bloqueante, para o
  laço de 10 ms nunca ficar parado esperando a conversão. Aceitei a espera de
  20 ms na partida depois de ver nos eventos que o buzzer da saída tocava sem
  motivo em t = 278 µs; os fins de curso só são conferidos com as entradas
  já estáveis, e o motor fica desligado nesse intervalo.
- **Como verifiquei:** eventos do cenário `partial-startup`: buzzer da saída
  acionado em t = 278 µs antes da correção e nenhuma vez depois;
  `partial-startup` e `contradictory-limits` passam; suítes `unit:adc`,
  `unit:barrier`, `unit:hw` e `unit:lane` passam (98,9% linhas, 88,2%
  branches no parcial). Na suíte `hw`, o ADC real lê ~4095 nos dois canais.
- **O que aprendi / o que ainda preciso entender:** aprendi o fluxo do ADC
  assíncrono do Zephyr (sequência, buffer estático que precisa existir até o
  fim da conversão, `k_poll_signal_check`) e a converter distância em código
  do ADC (683 e 1024 para a histerese). Aprendi que logo após o reset as
  entradas ainda não refletem o hardware. Preciso revisar como o driver do
  STM32U5 trata a calibração do ADC4.

### 2026-11-18 — Montador de linhas, protocolo da câmera e sessões

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-11, REQ-02 (sintaxe), REQ-04 (tabela)
- **O que pedi:** as tarefas T7, T8 (parte de protocolo) e T9 do plano.
- **O que a IA propôs:** `src/line_parser.c` (quadro de 128 bytes com o LF,
  modo CRLF ou só LF, ASCII imprimível, prazo de montagem de 1000 ms,
  descarte até o LF, `lp_parse_uint` para decimais canônicos);
  `src/camera.c` (`AT+CAPTURE`, `OK`, `ERROR`, `+PLATE` estrito, expressão
  da placa); `src/sessions.c` (tabela por placa com capacidade do Kconfig).
  Suíte `tests/proto`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei um montador de
  linhas único para os dois protocolos (CRLF da câmera, LF do pagamento),
  porque as regras de quadro (128 bytes, 1000 ms, descarte até o LF) são as
  mesmas. Aceitei a análise estrita das respostas: qualquer campo fora do
  formato descarta a linha inteira em vez de tentar aproveitar parte dela.
  Conferi a correção do teste de 22 bytes contando os caracteres.
- **Como verifiquei:** suíte `unit:proto` passa (um teste errado de contagem
  de bytes foi corrigido: `AT+CAPTURE=999999999\r\n` tem 22 bytes).
- **O que aprendi / o que ainda preciso entender:** aprendi a tratar bytes
  de fora como não confiáveis: limite de tamanho, prazo de montagem,
  descarte até o fim de linha e análise de números sem `atoi` (zeros à
  esquerda, estouro). Aprendi a contar bytes de protocolo com cuidado
  (o CRLF conta dois). Preciso revisar a expressão completa da placa
  Mercosul e a antiga.

### 2026-11-18 — UART por interrupção e ciclo completo da entrada

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-02, 03, 04, 06, 07, 08, 09, 11
- **O que pedi:** as tarefas T6, T8 (UART) e T10 do plano.
- **O que a IA propôs:** recepção por interrupção em `src/hw.c` (ISR esvazia
  a FIFO num `ring_buf` por UART, um produtor e um consumidor, sem trava) e
  transmissão por `uart_poll_out`; `src/lane.c` com os estados IDLE,
  IDENTIFY, OPENING, PASSAGE, CLOSING, ASSIST e LOCKED: 3 tentativas com ID
  novo e 1000 ms, histerese 683/1024 com a flag `armed`, abandono ao se
  afastar, decisão por sessões, verde só com a cancela aberta e o motor
  parado, passagem por feixe ativo→inativo e distância > 1024, sessão criada
  só na passagem, fechamento com verde apagado, rearme da assistência com o
  carro longe e o feixe livre. Suíte `tests/lane` reescrita (falsos de UART
  com injeção de bytes).
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a UART por
  interrupção com `ring_buf` (escolha minha no plano), porque a FIFO do
  hardware é pequena e o laço de 10 ms poderia perder bytes. Aceitei criar a
  sessão só quando o carro passa de fato (feixe ativo→inativo e distância
  acima de 1024), e não ao abrir a cancela, para um carro que desiste não
  ficar registrado como estacionado.
- **Como verifiquei:** passam `confidence-retry`, `low-confidence`,
  `camera-timeout`, `stationary`, `noisy-threshold`, `jam`, `missing-upper`,
  `missing-lower`, `obstruction-before-close`, `obstruction-during-close`,
  `partial-startup` e `contradictory-limits` (os 4 do portão G3 incluídos).
  `normal` falha na saída (pagamento ainda não implementado), como esperado.
  Todas as suítes passam.
- **O que aprendi / o que ainda preciso entender:** aprendi o padrão de
  buffer circular com um produtor (ISR) e um consumidor (laço), que dispensa
  trava, e por que a ISR deve esvaziar a FIFO toda. Aprendi a usar histerese
  com uma flag de rearme para o ruído do sensor não disparar a câmera várias
  vezes. Preciso revisar o que acontece se o `ring_buf` encher (bytes
  descartados e o quadro perdido).

### 2026-11-18 — Saída: tarifa, display, pagamento e prazo do motor por work

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-05, REQ-06, REQ-12, REQ-13
- **O que pedi:** as tarefas T11 a T14 do plano.
- **O que a IA propôs:** `src/tariff.c` (inteiros, mínimo 1 unidade, limite
  de 9999 unidades, `BRL r.cc`); `src/payment.c` (CHARGE/CANCEL, RESULT e
  ERROR estritos, UUID v4 por xorshift64* misturando ciclos da CPU e um
  contador); `src/display.c` + `src/font.c` (reset por hardware, SWRESET,
  SLPOUT, COLMOD 55, MADCTL 00, DISPON; texto por CASET/RASET/RAMWR com fonte
  5x7 própria ampliada 2x); estado PAYING em `src/lane.c`: desenha a placa,
  congela a tarifa, desenha valor e avisos, envia o CHARGE; reenvia a mesma
  linha até 3 vezes a cada 1000 ms; só PAID com o UUID ativo e o mesmo valor
  abre; desistência invalida o UUID antes do CANCEL e pede assistência.
  Achado na medição: desenhar a tela leva ~40 ms (build de cobertura); para o
  prazo de 2500 ms do motor não depender do laço, a mesma work de parada é
  agendada (`k_work_delayable`) para o instante do prazo.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a tarifa só com
  inteiros (sem `float`), o reenvio da mesma linha CHARGE com o mesmo UUID e
  a invalidação do UUID antes do CANCEL, para que um PAID atrasado nunca abra
  a cancela. Aceitei agendar o prazo do motor numa `k_work_delayable` depois
  da medição de ~40 ms do desenho da tela, porque com o prazo só no laço o
  motor poderia passar dos 2500 ms. Aceitei a fonte própria em vez de uma
  biblioteca gráfica, por ser pequena e fácil de explicar.
- **Como verifiquei:** os 19 cenários públicos passam; todas as suítes
  passam; cobertura 97,8% de linhas e 88,6% de branches
  (`firmware-coverage.py`, build/full-01).
- **O que aprendi / o que ainda preciso entender:** aprendi a calcular a
  tarifa com arredondamento para cima só com inteiros, a sequência de
  inicialização do ST7789V (SWRESET, SLPOUT, COLMOD, MADCTL, DISPON) e o
  envio de pixels RGB565 por CASET/RASET/RAMWR. Aprendi que um prazo de
  segurança não pode depender de um laço que pode atrasar. Preciso revisar
  por que xorshift64* é suficiente aqui e não seria em produção (não é
  criptográfico).

### 2026-11-18 — Análise estática e cobertura

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** qualidade (análise estática e cobertura)
- **O que pedi:** as tarefas T15 e T16 do plano.
- **O que a IA propôs:** correções dos 3 achados do `static-check.py`
  (`SPI_OP_MODE_MASTER` depreciado → `SPI_OP_MODE_CONTROLLER`; cast que
  descartava `const` no buffer SPI; `switch` sem `LANE_BOOT`/`LANE_LOCKED`);
  checagem de `snprintf` com um teste só (`(size_t)n >= size` também pega
  `n < 0`); nova suíte `tests/display` (sequência de inicialização, janelas e
  cada ponto de erro da SPI) e testes de pagamento na suíte `lane` (PAID
  exato, UUID/valor errados, 3 envios idênticos + CANCEL, CANCELLED,
  abandono, estouro da tarifa, falha do display e da UART).
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei as três
  correções da análise estática e os testes novos. Não aceitei, por ora,
  excluir da cobertura (LCOV_EXCL) os ramos de erro de inicialização dos
  drivers em `hw.c` e `main.c`: prefiro deixá-los visíveis como não cobertos
  e explicar na sabatina que só ocorrem com falha de hardware real.
- **Como verifiquei:** `static-check.py`: 0 achados; `firmware-coverage.py`:
  19/19 cenários e 6/6 suítes passam, 99,1% linhas e 94,7% branches.
- **O que aprendi / o que ainda preciso entender:** aprendi a ler os
  relatórios do `-fanalyzer` e do gcov (linhas x branches) e a escrever
  testes que forçam cada ponto de erro (falso que falha na n-ésima chamada).
  Preciso estar pronto para explicar na sabatina quais ramos ficaram sem
  cobertura e por quê.

### 2026-11-23 — Atualização para a v1.0.1 do laboratório

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** —
- **O que pedi:** analisar e incorporar a correção publicada na v1.0.1.
- **O que a IA propôs:** a v1.0.1 muda só `scripts/lab.py` (aceita qualquer
  data de build do Renode 1.17.0 do commit `f1dd1b4af`), o teste dele e a
  documentação de instalação; nenhum requisito do desafio mudou. Descartar o
  ajuste local do `lab.py` e fazer merge da tag `v1.0.1` no branch `solucao`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei; a correção oficial
  substitui o ajuste local, e o `lab.py` voltou a ser o do curso, sem mudança.
- **Como verifiquei:** `tests/test_lab.py` (10 testes) passa;
  `firmware-coverage.py` com o `lab.py` oficial: 19/19 cenários e 6/6 suítes
  passam, 99,1% linhas e 94,7% branches.
- **O que aprendi / o que ainda preciso entender:** aprendi a incorporar uma
  versão nova do curso por merge de tag, sem reescrever o histórico já
  publicado, e a conferir pelo texto do PDF se algum requisito mudou antes
  de mexer no código.

### 2026-11-24 — Testes extras contra os cenários ocultos; passagem tardia

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-04, REQ-07 (e regressão de todos)
- **O que pedi:** mais testes de cenário para reduzir as surpresas dos
  cenários ocultos.
- **O que a IA propôs:** (1) rodar o simulador de fila de carros do próprio
  laboratório (`scripts/assess.py --lot`) com perfis variados: câmera que
  falha ou não responde, pagamento atrasado, cancelado, malformado ou sem
  resposta, placa lida errada na saída, motorista que hesita ou desiste no
  pagamento, carro parado no feixe, obstrução no fechamento e lotação;
  (2) 22 cenários novos escritos à mão, fora de `student/` e não entregues:
  as duas faixas ao mesmo tempo, falhas na faixa de saída enquanto a entrada
  trabalha, resposta antiga da câmera chegando durante o pedido seguinte,
  abandono na identificação e retorno, carro parado na faixa de histerese com
  ruído, cancelamento no terminal, reset que apaga a sessão, permanência longa
  e respostas byte a byte. O simulador achou uma falha: um carro que ficava
  no feixe além dos 10 s gerava assistência (correto), mas, quando terminava
  de passar, o firmware não criava a sessão. O avaliador conta a sessão pela
  passagem física, então a lotação do firmware ficava diferente da real
  (risco de `capacity_exceeded`) e esse carro não conseguia sair. Correção em
  `lane.c`: `assist` marca `passage_late` quando interrompe uma passagem, e
  `late_passage_step`, chamada no estado LOCKED, registra a passagem quando
  ela termina, sem nunca mexer na cancela. Três testes de unidade novos.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a correção, porque
  segue a passagem real e mantém a cancela travada até o reset (REQ-07 e
  REQ-09 continuam valendo). Três cenários meus falharam por erro de
  montagem, não do firmware, e foram corrigidos: o carro recuava antes da
  primeira captura (o firmware só começa por volta de 410 ms, depois da
  inicialização do display); a resposta byte a byte passava de 1000 ms (o
  descarte é o comportamento exigido); a janela do verde era apertada demais.
- **Como verifiquei:** fila de carros: 5 perfis, todos os veredictos passam
  (o `linger_in_beam` falhava antes da correção e passa depois); 22/22
  cenários novos e os 2 extras antigos passam; `firmware-coverage.py`:
  19/19 cenários públicos e 6/6 suítes passam, 99,1% linhas e 94,8%
  branches; `static-check.py`: 0 achados.
- **O que aprendi / o que ainda preciso entender:** aprendi que o estado do
  firmware (sessões) precisa acompanhar o mundo físico mesmo depois de uma
  falha, e que um cenário que falha pode estar errado ele mesmo: antes de
  mudar o código, conferir nos eventos o que de fato aconteceu.

### 2026-11-24 — Testes com capacidade 1

- **Aluno:** Jeyson Nascimento
- **Requisito(s):** REQ-04 (lotação) e regressão de todos
- **O que pedi:** rodar o `full-lot`, os cenários públicos e os 22 cenários
  extras com o firmware compilado com capacidade 1.
- **O que a IA propôs:** compilar com `lab.py --capacity 1` (confirmado
  `CONFIG_PARKING_CAPACITY=1` no `.config`) e rodar tudo nesse build, mais
  uma fila de carros de rodízio (cada carro fica 3 s) para mostrar que a vaga
  é liberada na saída e ocupada de novo. Nenhuma mudança de código.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. Três cenários
  extras (`x-dual-simultaneous`, `x-dual-delayed-retry` e
  `x-exit-jam-entry-busy`) falham com capacidade 1 porque foram escritos
  supondo mais de uma vaga: o segundo carro é identificado na entrada
  enquanto o primeiro ainda está dentro (pago, mas sem ter cruzado a saída,
  ou com a cancela de saída emperrada), e o firmware recusa a entrada. É o
  comportamento correto: a vaga só é liberada pela passagem física, como o
  avaliador conta. Conferido nos resultados: capacidade 1, 2 capturas e
  1 abertura na entrada.
- **Como verifiquei:** com capacidade 1, `full-lot` passa; fila do lote cheio
  8/8 e fila de rodízio 6/6 veredictos; 19/19 cenários públicos passam;
  19/22 cenários extras passam e os 3 restantes dependem de mais de uma vaga.
- **O que aprendi / o que ainda preciso entender:** aprendi que a capacidade
  é uma opção de build (1 a 8) que os cenários ocultos podem usar, e que
  pagar não libera a vaga: só a passagem pela saída libera.

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

Nenhum trecho foi copiado. As referências usadas como base foram:

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| `src/payment.c`, gerador xorshift64* (deslocamentos 12/25/27 e multiplicador `0x2545F4914F6CDD1D`) | algoritmo publicado por G. Marsaglia (xorshift) e S. Vigna (variante `*`) | algoritmo de domínio público; código escrito para o projeto |
| `src/hw.c`, uso de GPIO, ADC assíncrono, UART por interrupção, `ring_buf` e SPI | documentação da API do Zephyr v4.5.0 | Apache-2.0 (Zephyr); código escrito para o projeto |
| `src/display.c`, comandos e esperas do ST7789V | `docs/hardware-models.md` e `docs/desafio.md` do laboratório | — |
| `src/font.c`, fonte 5x7 | desenhada à mão para o projeto | — |
