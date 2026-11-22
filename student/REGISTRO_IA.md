# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| Maria Antonia Ribeiro Ramos | 21211260 | Cancelas, segurança, ADC4, controle de faixa, sessões e integração. |
| Jackson Bruno Lima Leao | 21111301 | Câmeras, pagamento, tarifa e display SPI. |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| Maria Antonia Ribeiro Ramos | Codex | Versão não informada | 18–19/11/2026 |
| Jackson Bruno Lima Leao | Claude (Anthropic) | Claude Sonnet 5.5, conforme registro do aluno | 18–19/11/2026 |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-18 — Interfaces e cenário inicial (issue #1)

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex, versão não informada.
- **Requisitos:** REQ-01 a REQ-13.
- **Pedido:** iniciar a issue #1 com a proposta de interfaces e a execução do cenário inicial.
- **Proposta:** divisão de responsabilidades, estados e prazos em `student/INTERFACES.md`.
- **Aceitação ou revisão:** pendente pela dupla.
- **Verificação:** a tentativa local x86_64 parou na verificação da versão do Renode. Em ARM64 emulado, o build e a preparação passaram, mas a inicialização expirou. A execução `issue-01-actions-normal` no GitHub Actions ARM64 nativo (job 44333386814) confirmou a falha esperada da base no tick 590, aos 5,90 s virtuais. Os resultados estão em `student/INTERFACES.md`.
- **Aprendizado da aluna:** a preencher após a revisão.

### 2026-11-18 — Segurança das cancelas (issue #2)

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex, versão não informada.
- **Requisitos:** REQ-01, REQ-06, REQ-08 e REQ-09.
- **Pedido:** seguir para a implementação da segurança das cancelas.
- **Proposta:** máquina de estados testável em `gate.c`, GPIO do devicetree e EXTI com trabalho diferido em `main.c`, e testes ztest/FFF para movimentos, obstrução e assistência.
- **Aceitação ou revisão:** a revisar pela dupla.
- **Verificação:** [job ARM64 44335948702](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44335948702). `partial-startup` e `contradictory-limits` passaram até o checkpoint de 6,00 s virtuais (tick 600). Os oito testes `gate_safety` passaram; a cobertura parcial foi de 82,84% das linhas e 67,42% dos branches, e a análise estática teve zero achados. Os cenários `jam`, `missing-lower` e `obstruction-during-close` ainda precisam da integração com reconhecimento e passagem para acionar o movimento no firmware.
- **Aprendizado da aluna:** a preencher após a revisão.

### 2026-11-18 — Validação de todos os cenários públicos

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Pedido:** rodar todos os cenários oficiais com o estado atual do firmware.
- **Verificação:** [job ARM64 44341017271](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44341017271), commit `abc3f08`. Passaram 2 de 19 cenários (`partial-startup` e `contradictory-limits`), a suíte unitária e a análise estática com zero achados. Cobertura de 82,84% das linhas e 67,42% dos branches.
- **Falhas observadas na issue #2:** `jam` esperava uma captura aos 6,00 s virtuais (tick 600) e observou zero. `missing-lower` e `obstruction-during-close` esperavam uma transição para verde e observaram zero. Esses cenários ainda não chegaram a exercitar a falha de movimento pretendida. O artefato do job preserva o resultado dos 19 cenários.

### 2026-11-18 — ADC4, presença, passagem e sessões

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex, versão não informada.
- **Pedido:** implementar as issues #3 e #4, preservando as atribuições do Jackson.
- **Proposta:** aquisição ADC4 assíncrona e controle de sessão e passagem, com adaptador para as operações de câmera e pagamento.
- **Verificação:** [job ARM64 44371350008](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44371350008). O teste do driver ADC4 leu os dois canais no Renode; o teste `parking_flow` confirmou abandono, criação de sessão por passagem e liberação da vaga após a saída paga. Passaram as duas suítes e os cenários `partial-startup` e `contradictory-limits`. Cobertura parcial de 83,57% das linhas e 68,18% dos branches; zero achados na análise estática. Os cenários que exigem captura real, pagamento ou passagem completa aguardam o adaptador da dupla.
- **Aceitação e aprendizado:** a revisar pela aluna e pela dupla.

### 2026-11-18 — Câmeras: parser UART e captura (issue #5)

- **Aluno:** Jackson Bruno.
- **Ferramenta:** Claude (Anthropic), modelo Claude Sonnet 5.5, interface web.
- **Requisito(s):** REQ-02, REQ-11 (parte da câmera) e a parte de câmera de REQ-03 e REQ-13.
- **O que pedi:** a solução da issue #5, com explicação didática de cada arquivo, e o fluxo de git por branch.
- **O que a IA propôs:** `student/src/camera_parser.{h,c}` (montagem de linhas CRLF, 128 bytes, prazo de 1000 ms, descarte até LF),
  `camera.{h,c}` (3 tentativas, id novo, confiança >= 85, `capture_start/poll/cancel` no formato de `struct parking_equipment`),
  `camera_hw.{h,c}` (UART por polling via alias do devicetree) e as suítes `tests/camera` e `tests/camera_hw`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei polling em vez de IRQ porque o contrato do modelo não promete transporte por interrupção, e adicionei `DEFINE_FFF_GLOBALS;` em `test_camera.c` para resolver a linkagem do ztest/FFF no Renode.
- **Como verifiquei:** compilei e executei no ambiente oficial Linux/WSL com Zephyr v4.5.0 e Renode 1.17.0 usando `firmware-coverage.py` (`build/cov-check`): cenários `confidence-retry`, `low-confidence`, `camera-timeout` e `reset-capture` passando, além das suítes unitárias `camera` e `camera_hw` passando com 100.00% de cobertura de linhas e branches em `camera.c` e `camera_parser.c`, e 100.00% de linhas / 90.00% de branches em `camera_hw.c`.
- **O que aprendi / o que ainda preciso entender:** compreendi o protocolo AT fragmentado da câmera, o uso do devicetree por alias (`entry-camera`/`exit-camera`) sem números de pino fixos e a separação entre lógica pura (`camera.c`) e camada fina de hardware (`camera_hw.c`) para testes com FFF.

### 2026-11-19 — Terminal de pagamento LPUART1 e cálculo de tarifa (issue #6)

- **Aluno:** Jackson Bruno.
- **Ferramenta:** Claude (Anthropic), modelo Claude Sonnet 5.5, interface web.
- **Requisito(s):** REQ-04, REQ-05, REQ-10, REQ-11 (parte de pagamento) e parte de pagamento de REQ-03 e REQ-13.
- **O que pedi:** a implementação do cliente do terminal de pagamento (LPUART1), cálculo de tarifa, geração de UUID v4 de transação e suítes unitárias da issue #6.
- **O que a IA propôs:** `student/src/tariff.{h,c}` (cálculo proporcional por unidade de tarifa com teto), `payment_uuid.{h,c}` (geração de UUID v4), `payment_parser.{h,c}` (montagem CRLF e parse de respostas do terminal), `payment.{h,c}` (máquina de estados de cobrança com retentativas e cancelamento) e `payment_hw.{h,c}` (LPUART1 via alias `payment-terminal`), além das suítes `tests/payment` e `tests/payment_hw`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a separação entre lógica pura (`payment.c`, `tariff.c`, `payment_parser.c`) e hardware (`payment_hw.c`), atualizei `student/CMakeLists.txt` e adicionei `DEFINE_FFF_GLOBALS;` em `test_payment.c` para linkagem no Zephyr/Renode.
- **Como verifiquei:** compilei e executei no ambiente oficial Linux/WSL com Zephyr v4.5.0 e Renode 1.17.0 usando `firmware-coverage.py` (`build/cov-issue06`): todas as 6 suítes unitárias (`unit:payment`, `unit:payment_hw`, `unit:camera`, `unit:camera_hw`, `unit:distance`, `unit:unit`) e os cenários `partial-startup` e `contradictory-limits` passaram, atingindo 100.00% de cobertura de linhas e branches em `payment.c`, `payment_parser.c`, `payment_uuid.c` e `tariff.c` (e 100.00% de linhas / 85.71% de branches em `payment_hw.c`).
- **O que aprendi / o que ainda preciso entender:** compreendi o protocolo textual do terminal de pagamento na LPUART1, idempotência via UUID por sessão de cobrança e cálculo determinístico de tarifa por tempo decorrido.
### 2026-11-19 — Display colorido ST7789V via SPI1 e renderização de texto (issue #7)

- **Aluno:** Jackson Bruno.
- **Ferramenta:** Claude (Anthropic), modelo Claude Sonnet 5.5, interface web.
- **Requisito(s):** REQ-05, REQ-10, REQ-11 (parte de display SPI) e parte de exibição de REQ-03 e REQ-13.
- **O que pedi:** a implementação do driver e renderizador do display ST7789V (240x320 RGB565 via SPI1), fonte 5x7 e suítes unitárias da issue #7.
- **O que a IA propôs:** `student/src/font5x7.{h,c}` (mapa de caracteres ASCII 5x7), `display_render.{h,c}` (geração de buffer RGB565), `display.{h,c}` (sequência de inicialização ST7789V, janelas CASET/RASET/RAMWR e exibição de placa/valor) e `display_hw.{h,c}` (SPI1 e pinos CS, D/C e RESET via alias `payment-display`), além das suítes `tests/display` e `tests/display_hw`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a separação entre renderização em memória, protocolo ST7789V e camada fina SPI/GPIO, e atualizei `student/CMakeLists.txt`.
- **Como verifiquei:** revisão estrutural dos arquivos e preparação das suítes unitárias `tests/display` e `tests/display_hw`; execução no Renode (`firmware-coverage.py` e `static-check.py`) pendente para a etapa de integração junto com a issue #6 (`payment.h`).
- **O que aprendi / o que ainda preciso entender:** compreendi a inicialização e os comandos do controlador ST7789V (COLMOD 0x55, MADCTL 0x00, janelas de escrita RAMWR) e o controle explícito dos pinos D/C, CS e RESET no Zephyr.

### 2026-11-19 — Integração de câmera, pagamento e display

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex.
- **Pedido:** integrar os módulos publicados pelo Jackson ao controlador das duas faixas e validar no Renode.
- **Alterações:** integrar os commits das issues #6 e #7, registrar os callbacks de câmera e pagamento em `main.c` e iniciar o display SPI. Preservar as entradas dos dois autores. Isolar o estado do display entre testes e copiar os caracteres dentro dos limites do campo.
- **Verificação inicial:** [job ARM64 44650739379](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44650739379). Os 19 cenários públicos passaram. A suíte de display detectou dependência da ordem dos testes e a análise estática acusou a cópia por `strncpy`.
- **Verificação após a correção:** [job ARM64 44652563506](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44652563506), firmware `6bb5b5a`. Passaram os 19 cenários públicos e os 123 testes unitários. Cobertura de 96,15% das linhas e 88,58% dos branches. Análise estática com zero achados. Os vereditos, a cobertura e os logs estão no artefato `integracao-arm64`.
- **Revisão da dupla:** pendente.

### 2026-11-19 — Preparação da entrega e partida no WSL x86_64

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex.
- **Pedido:** abrir o PR de entrega e fornecer o link do Classroom.
- **Correção:** depois de incorporar a atualização oficial `82d854b`, o build padrão x86_64 expôs uma leitura de fins de curso antes da primeira atualização de 10 ms do modelo. O firmware mantém as saídas seguras e só avalia as entradas após 15 ms de estabilização.
- **Verificação:** `entrega-normal-x86-01` falhou no tick 590 (5,90 s virtuais). `entrega-normal-x86-02` confirmou o cenário `normal` completo no Renode oficial x86_64.
- **Validação completa:** [job ARM64 44658985711](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44658985711), firmware `bf9e724`. Passaram 19 cenários públicos e 123 testes unitários. Cobertura de 96,17% das linhas e 88,61% das ramificações. Análise estática com zero achados.

### 2026-11-22 — Organização dos contratos e adaptadores

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex.
- **Pedido:** aplicar as sugestões de organização repassadas pelo Jackson Bruno.
- **Alterações:** mover cabeçalhos para `include/` e os adaptadores de câmera, terminal, display e ADC4 para `src/drivers/`. Atualizar o CMake da aplicação e das oito suítes. Documentar os tipos públicos em Doxygen.
- **Verificação:** [job ARM64 45066678160](https://github.com/anntimar/estacionamento-embarcados/actions/runs/45066678160), commit `4392a73`. Passaram os 19 cenários públicos e os 123 testes unitários. Cobertura de 96,17% das linhas e 88,61% das ramificações, com zero achados na análise estática. As alterações posteriores nos cabeçalhos foram verificadas como comentários.

### AAAA-MM-DD — título curto

- **Aluno:** quem fez esta tarefa.
- **Requisito(s):** REQ-xx
- **O que pedi:** resumo do pedido.
- **O que a IA propôs:** resumo, arquivos e funções afetados.
- **O que aceitei, mudei ou rejeitei, e por quê:**
- **Como verifiquei:** cenários (nome e resultado), cobertura, testes de
  unidade, análise estática, revisão manual.
- **O que aprendi / o que ainda preciso entender:**

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| Aplicação inicial de student/ e tests/unit/src/test_exemplo.c | [Base da disciplina](https://github.com/ecom042/emul-parking-lot/tree/2549c63/student) | Apache-2.0 |
