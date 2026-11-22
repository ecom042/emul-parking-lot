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
| A | Hermes Agent (CLI) | deepseek-v4.1-flash-1017 | 21/11/2026, a partir das 20:30 |
| A | Hermes Agent (CLI) | (mesmo modelo) | 21/11/2026 — revisão do roadmap e da especificação técnica da dupla |

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
  `sys_rand_get` **combinado com contador e `k_uptime_get`**; `sessions.c` com
  vetor estático de `CONFIG_PARKING_CAPACITY` e `find()` linear.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a estrutura.
  Acrescentei `sessions_count()` porque os testes de "tabela cheia" precisam
  dele sem abrir a estrutura. Mantive o UUID com contador depois de conferir
  em `docs/device-protocols.md` que UUID repetido é reenvio idempotente: se a
  entropia do Renode for determinística, dois UUIDs iguais quebrariam
  `payment-duplicate`.
- **Como verifiquei:** 1000 UUIDs distintos com entropia constante e uptime
  parado (só o contador separando); tabela de tarifa conferida contra o
  contrato (60000→200, 60001→300, 2500 ms com unidade 1000→400, 9999
  unidades→1000000, 10000→`-ERANGE`). Suítes ztest escritas em
  `student/tests/tariff`, `sessions` e `uuid` — **ainda não executadas**: o
  workspace desta máquina não tem `west`, Zephyr 4.5.0 nem Renode instalados.
  A lógica equivalente foi executada num harness de host (1156 verificações,
  0 falhas).
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
  hardware antes do primeiro byte válido. **Não executado na placa**: falta o
  ambiente (Zephyr, Renode).
- **O que aprendi / o que ainda preciso entender:** por que o modelo do display
  exige `DISPON` antes do primeiro `RAMWR`, e por que a fonte é enviada linha a
  linha em vez de guardar a tela em RAM.

## Código de terceiros

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| `src/font.c` — tabela 5x7 | desenhada para este projeto (cada glifo conferido em arte ASCII) | Apache-2.0, do próprio projeto |
| `src/hw_uart.c` — laço `uart_fifo_read`/`fifo_fill` | padrão do driver UART interrupt-driven do Zephyr | Apache-2.0 |
