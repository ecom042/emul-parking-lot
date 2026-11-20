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
| Pablo | Codex | GPT-6 | 18/11/2026 |
| Matheus | Claude Code | Claude Opus 5.5 | 19/11/2026 |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-18 — Base do controlador

- **Aluno** Pablo.
- **Requisitos** REQ-01, REQ-03, REQ-06 a REQ-10 e pontos de integração
  para REQ-02, REQ-04, REQ-05 e REQ-13.
- **Pedido** Analisar os contratos e implementar o núcleo das duas faixas,
  com GPIO/EXTI, ADC4 assíncrono e interfaces para os módulos da dupla.
- **Proposta da IA** Usar duas instâncias de `lane.c`, concentrar as saídas
  em `parking_gpio.c`, isolar a aquisição em `parking_adc.c` e trocar pedidos
  e resultados correlacionados pelo contrato de `parking.h`.
- **Aceitação ou alterações** Aguardando revisão do aluno.
- **Verificação** Passaram 14 testes da lógica em C no Windows com GCC e FFF,
  usando asserções C para executar os casos da suíte. A análise local do núcleo
  com `-fanalyzer` terminou sem diagnósticos. Build da placa, ztest no Renode,
  cenários e métricas do curso pendentes.
- **Para a sabatina** Explicar o caminho EXTI, thread de controle e motor
  desligado. Explicar por que a memória da conversão continua válida após timeout.

### 2026-11-18 — Validação da base no ambiente do curso

- **Aluno** Pablo.
- **Pedido** Validar `solucao` no commit `430cfc9`, conferir os portões de
  segurança aplicáveis e manter a divisão da dupla.
- **Proposta da IA** Reusar o workspace west existente e executar as
  ferramentas do curso. Diante da recusa de identidade do launcher, executar
  também `west build`, `prepare-smoke.py` e `assess.py` diretamente, com
  o firmware da base e os periféricos do Renode.
- **Aceitação ou alterações** Validação pedida pelo aluno. Execução
  complementar registrada para revisão. Alteração versionável restrita a este
  registro, sem commit ou push.
- **Ambiente** Checkout principal e cópia de validação limpos no início, ambos
  em `430cfc95dc901921ab159fdb4e18aa79b740da3e`, também apontado pela referência
  local `origin/solucao`. West 1.4.0, Python 3.12.7, Zephyr 4.5.0
  (`41d10b597304`), SDK 1.0.1 (GCC Arm 14.3.0) e ZVB `aed53b3ceda`.
  `ZVB_MODULE` e `RENODE_ROOT` apontaram para os diretórios irmãos
  `zephyr-virtual-board` e `renode` no workspace existente. A primeira
  rodada de cobertura e análise parou antes de compilar por falta de
  `jsonschema`. A instalação de `zephyr/scripts/requirements-base.txt` no
  venv local permitiu repetir as verificações.
- **Bloqueio do launcher** Os três comandos de `lab.py` retornaram 2 antes do
  build. O SHA-256 do pacote portátil Linux x86_64 coincide com o do guia
  (`4ba7c68b59e2447f188ef4b4b112fcccd2802582c460beedceb63b84e5605a5f`).
  O executável informa `1.17.0+20260907gitf1dd1b4af` e o launcher exige
  `1.17.0+20260906gitf1dd1b4af`. Isso explica a ausência dos
  `assessment/results.json` nessas invocações.
- **Build e comportamento** O build e a preparação separados geraram o ELF
  e a máquina com a unidade de tarifa de 5000 ms. Com outro build de 1000 ms,
  sem instrumentação de cobertura, `assess.py` aprovou `partial-startup` e
  `contradictory-limits`. Aos 600 ticks, a entrada manteve motor e verde
  desligados, vermelho aceso e um pulso de buzzer em cada cenário. A cancela
  permaneceu em 45000 mdeg no primeiro (`1d158fab540f4420b317352c4df4b4d2`) e em
  0 mdeg no segundo (`2552e13202594359a8d8a7f9d0e0c5fb`). No segundo,
  ambos os fins de curso ficaram ativos a partir de 10000 µs virtuais e o
  buzzer foi acionado a 10012 µs. Os pulsos no rastro GPIO duraram 100011 µs e
  99097 µs, respectivamente. Os cenários verificam a contagem de pulsos e o
  ztest verifica o prazo de 100 ms no relógio da lógica.
- **ztest/FFF e cobertura** `firmware-coverage.py --scenario partial-startup
  --jobs 1` aprovou o cenário instrumentado e os 14 testes da suíte `unit`
  na placa simulada. Cobertura desta seleção de 292/363 linhas (80,44%) e
  178/280 ramos (63,57%). O mapa registra execução de código de EXTI de fim de
  curso e publicação de amostras do ADC4 no cenário. Os testes FFF de
  obstrução, travamento e falta do fim de curso inferior exercitam a lógica
  com o driver substituído por falsos.
- **Análise estática** `static-check.py` finalizou com `build_ok: true` e
  zero achados em `student/src`. O log contém avisos do Zephyr e do HAL.
- **Evidências** Na cópia de validação em
  `build/validacao-430cfc9-01/workspace/emul-parking-lot`, a rodada
  `build/runtime-03/` contém `ambiente.log`, `*.cli.log`,
  `prepare/renode-version.log`, `complementar/comandos.log`,
  `complementar/prepare-5000/`, os dois
  `complementar/<cenário>/assessment/results.json` e os rastros em
  `assessment/runs/`. Também contém `coverage/summary.json`,
  `coverage/unit/unit/console.log` e `static/summary.json`. A rodada
  `build/runtime-02/` conserva as falhas da primeira tentativa.
- **Pendência da dupla** O checkout contém o contrato de `parking.h`, mas
  nenhum serviço registrado de câmera, pagamento P1, sessões, tarifa ou
  display. `jam` exige captura e abertura autorizada para testar o prazo
  do motor. `missing-lower` e `obstruction-during-close` exigem também a
  passagem para iniciar o fechamento. Os três dependem de câmera e
  autorização/sessão da entrada. `normal`, `payment-*` e `reset-payment`
  dependem também da integração de saída, que inclui pagamento, tarifa e display.
  Próxima verificação após integrar os serviços em `parking.h`: rodar os
  quatro G3 com a identidade do Renode alinhada pelo curso.

### 2026-11-18 — Testes do ADC4 assíncrono

- **Aluno** Pablo.
- **Pedido e aceitação** O aluno pediu executar os testes propostos para o
  REQ-10: fronteira de 100 ms, conclusão tardia e falhas do driver.
- **Proposta aplicada** Criar `student/tests/adc/` com ztest/FFF. A suíte
  executa o código de `parking_adc.c` em uma thread Zephyr e usa o sinal
  de conclusão do kernel. Os falsos controlam as chamadas ao driver e o tempo
  observado pela aquisição. As substituições são exclusivas da suíte.
- **Verificação** `firmware-coverage.py --scenario partial-startup --jobs 1`
  aprovou `partial-startup`, a suíte ADC (7/7) e a suíte das faixas (14/14).
  Os testes confirmaram a publicação em 99 ms e a troca do canal 17 para o 18,
  rejeição em 100 e 101 ms, conclusão depois do timeout sem publicação ou
  nova submissão, e falha compartilhada entre as faixas para erro de
  submissão, erro de conclusão e código fora de 12 bits. Cobertura de
  `parking_adc.c`: 65/69 linhas (94,20%) e 28/32 ramos (87,50%).
  Cobertura total da seleção: 83,20% de linhas e 67,50% de ramos.
  `static-check.py` concluiu o build e registrou zero achados em `student/src`.
- **Evidências** Na cópia de validação em
  `build/validacao-430cfc9-01/workspace/emul-parking-lot`, a rodada
  `build/adc-01/` guarda os comandos e resultados em `*.cli.log`, o
  relatório em `coverage/summary.json`, os consoles em
  `coverage/unit/{adc,unit}/console.log` e o cenário em
  `coverage/scenarios/partial-startup/results.json`. O relatório do GCC está
  em `static/summary.json`.
- **Para a sabatina** A thread de aquisição espera a conclusão sem segurar a
  thread de controle. Se o prazo expirar, o estado de falha fica travado.
  O driver ainda pode escrever no buffer e sinalizar porque o armazenamento
  é estático. A thread de controle continua sendo acordada pelo EXTI para ler
  os fins de curso e o feixe e decidir quando desligar o motor. A medição
  física dos 10 ms durante o fechamento continua pendente no cenário integrado.

### 2026-11-19 — Câmera UART e reserva de vaga na entrada

- **Aluno** Matheus.
- **Requisitos** REQ-02 (captura com três tentativas), REQ-03 (entrada com
  vaga livre, lotação e placa já estacionada) e o contrato `parking.h`
  (`request`, `passage`, `cancel`).
- **Pedido** Integrar a câmera UART da entrada e a reserva de vaga: novo ID
  por tentativa, resposta fragmentada, resultado com faixa, transação e
  tentativa corretas; reservar na autorização, confirmar em `passage()` e
  liberar em `cancel()`. Testar fragmentação, ID antigo, confiança baixa,
  lotação, placa duplicada e cancelamento.
- **Proposta da IA** Separar a lógica pura da camada Zephyr:
  `camera.[ch]` (parser CRLF limitado a 128 bytes, números canônicos, sintaxe
  da placa, formatação de `AT+CAPTURE`), `sessions.[ch]` (tabela
  FREE/RESERVED/ACTIVE; a reserva conta na lotação), `services.[ch]`
  (implementa `parking.h`; os callbacks só copiam o pedido para uma fila,
  marcam a transação corrente sob spinlock e acordam o worker; o worker
  correlaciona o ID da câmera, reserva a vaga e reenvia a resposta em
  `-ENOMSG` até o prazo) e `parking_uart.c` (RX por interrupção em
  ring buffer, TX por `uart_poll_out`, thread preemptiva de prioridade 2).
  O núcleo continua controlando tentativas e prazos. As duas câmeras usam
  o mesmo código. A autorização de saída responde `PARKING_DENIED` até
  existir o pagamento P1, então a saída nunca abre sem `PAID`.
- **Alteração em arquivos do colega** `main.c` ganhou a chamada
  `parking_services_start()` depois de `parking_adc_init()`; `CMakeLists.txt`
  e `prj.conf` (`CONFIG_UART_INTERRUPT_DRIVEN=y`) foram atualizados.
  `parking.h`, `lane.[ch]` e os demais módulos de Pablo não mudaram.
  Revisão de Pablo pendente.
- **Aceitação ou alterações** Aguardando revisão do aluno e do colega. Sem
  commit.
- **Ambiente** Para rodar os cenários localmente, `scripts/lab.py` foi
  alterado só nesta máquina para aceitar também o build
  `1.17.0+20260907gitf1dd1b4af` do Renode oficial (mesmo commit). A
  alteração está fora de `student/`, marcada com `skip-worktree` e não
  será versionada; aguarda esclarecimento do professor.
- **Verificação** `lab.py --headless` aprovou `jam`, `missing-lower`,
  `obstruction-during-close`, `contradictory-limits` e `partial-startup`.
  `normal` passou nos checkpoints da entrada (ticks 590 e 990: abertura com
  uma captura, passagem e fechamento) e falhou no primeiro da saída
  (tick 1590, `payments: 1`), como esperado sem pagamento.
  `firmware-coverage.py` com esses cinco cenários aprovou as suítes `adc`,
  `unit` e a nova `protocols` (camera, sessions, services com FFF):
  96,22% de linhas e 83,27% de ramos no total; `camera.c`, `services.c`,
  `sessions.c` e `parking_uart.c` com 100% de linhas. `static-check.py`
  apontou dois achados (`-Wcast-qual` em `sessions.c`, `-Wswitch-enum` em
  `services.c`), corrigidos; a segunda rodada terminou com zero achados.
- **Evidências** `build/cam-<cenário>-01/assessment/results.json`,
  `build/cam-coverage-02/summary.json` e `build/cam-static-02/summary.json`.
- **Para a sabatina** Explicar por que `request()` grava a transação
  corrente e enfileira no mesmo spinlock e por que o worker checa de novo
  antes de reservar. Explicar por que `OK` não é reconhecimento, por que um
  ID antigo é ignorado e por que a reserva conta na lotação antes da
  passagem.

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| | | |
