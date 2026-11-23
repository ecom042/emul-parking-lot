# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| Pablo Munih Silva de Carvalho | 22210730 | Núcleo das faixas, ADC4 e integração da saída |
| Matheus Melo dos Santos | 21212186 | Câmeras UART e tabela de sessões |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| Pablo | Codex | GPT-6 | 18/11/2026 |
| Matheus | Claude Code | Claude Opus 5.5 | 19/11/2026 a 21/11/2026 |

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
- **Aceitação ou alterações** Matheus revisou e aceitou após validação
  manual em 20/11/2026 e fez o commit `8148f3f` no branch
  `feat/protocolos`, enviado como PR #1. Pablo revisou o diff em 21/11
  (ver "Conferência de atualizações") e integrou o commit em `solucao`.
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
  Na validação manual de 20/11/2026 passaram também `confidence-retry`,
  `low-confidence`, `camera-timeout`, `reset-capture`, `stationary`,
  `noisy-threshold`, `missing-upper` e `obstruction-before-close`.
  `normal` e `payment-*` continuaram reprovando na saída, como esperado
  sem P1. Cobertura: 96,22% de linhas e 83,46% de ramos. A primeira
  análise estática quebrou o build por um texto digitado sem querer em
  `camera.c` (`ccc`). Matheus o removeu e a nova rodada terminou com zero
  achados.
- **Evidências** `build/cam-<cenário>-01/assessment/results.json`,
  `build/cam-coverage-02/summary.json`, `build/cam-static-02/summary.json`
  e, da validação de 20/11, `build/val-<cenário>/`, `build/val-coverage/`
  e `build/val-static-02/`.
- **Para a sabatina** Explicar por que `request()` grava a transação
  corrente e enfileira no mesmo spinlock e por que o worker checa de novo
  antes de reservar. Explicar por que `OK` não é reconhecimento, por que um
  ID antigo é ignorado e por que a reserva conta na lotação antes da
  passagem.

### 2026-11-21 — Conferência de atualizações

- **Aluno** Pablo.
- **Pedido** Conferir as atualizações do repositório, incluindo os PRs.
- **Proposta e execução** Revisar os dois commits locais da solução, as
  referências Git e os resultados da última validação arquivada. Sem
  alterações no firmware.
- **Verificação** As alterações dos dois commits ficam em `student/`.
  A última rodada arquivada confirma `partial-startup`, 7 testes do ADC
  e 14 das faixas, com 83,20% de linhas e 67,50% de ramos cobertos e
  zero achados estáticos. A consulta de PRs ficou pendente porque o GitHub
  exigiu autenticação na primeira tentativa. Na nova consulta, o PR #1
  estava aberto e sem conflitos, com um commit de Matheus (`8148f3f`)
  para câmera UART e reserva de vagas. Revisei o diff sem aplicar o PR.
  A revisão identificou perda silenciosa de bytes se o buffer RX lotar
  e divergência no percentual de ramos entre a descrição e o registro do
  PR. A validação relatada usa um launcher alterado na máquina do autor.
  Os cenários do PR não foram reexecutados nesta revisão.

### 2026-11-21 — Validação com a versão oficial v1.0.1

- **Aluno** Pablo.
- **Pedido** Considerar a atualização v1.0.1 anunciada pelo professor.
- **Proposta e execução** Usar a tag oficial (`82d854b`) numa cópia
  isolada com o conteúdo de `student/` do PR #1 (`8148f3f`), preservando
  o checkout da dupla. A correção oficial aceita a data do Renode Linux.
- **Verificação** Os 10 testes do launcher passaram. Com o Renode
  `1.17.0+20260907gitf1dd1b4af`, o launcher oficial compilou o firmware e
  aprovou `camera-timeout`, `confidence-retry`, `contradictory-limits`,
  `jam`, `low-confidence`, `missing-lower`, `missing-upper` e
  `noisy-threshold`. A execução parou em `normal`: no tick 1590, a
  cancela de saída permanecia em 0 mdeg, com 90000 mdeg esperados.
  O PR ainda não integra o pagamento. As evidências ficaram em
  `build/validacao-430cfc9-01/workspace/revisao-v101.u7Pf8N/build/pr1-v101-03/`,
  incluindo `headless.log` e `assessment/results.json`.

### 2026-11-21 — Referência de segurança para a integração da saída

- **Aluno** Pablo.
- **Pedido** Implementar o plano acelerado de integração com frentes UART,
  pagamento P1, tarifa e display.
- **Proposta e aceitação** Plano de seis horas aprovado para execução. A
  confirmação de entendimento dos estados, transições e prazos, prevista
  em `AGENTS.md`, foi solicitada antes das alterações de firmware.
- **Verificação** Em cópia da v1.0.1 oficial com o PR #1 (`8148f3f`),
  `obstruction-during-close` passou com `lab.py --headless`. As evidências
  dos quatro cenários G3 foram reproduzidas. A cobertura dos quatro G3
  mais `partial-startup` aprovou também as suítes `adc`, `unit` e
  `protocols` e mediu 96,22% de linhas e 83,27% de ramos. A análise
  estática terminou com zero achados.
- **Evidências** Na cópia já usada para validar a v1.0.1, os resultados
  estão em `build/g3-obstrucao-referencia/assessment/results.json`,
  `build/pr1-coverage-referencia/summary.json` e
  `build/pr1-static-referencia/summary.json`.

### 2026-11-21 — Integração da saída e robustez da UART

- **Aluno** Pablo.
- **Ferramenta** Codex (GPT-6), com três frentes de agentes em paralelo.
- **Pedido e aceitação** Implementar o plano acelerado. Pablo confirmou
  que entendeu os estados, as transições e os prazos antes da codificação.
- **Proposta aplicada** Integrar o commit de Matheus (`8148f3f`) e
  aproveitar o contrato das faixas. Em caso de perda, o transporte RX
  limpa a fila, descarta a linha afetada até LF e avisa o parser antes
  do próximo quadro íntegro. Um worker preemptível atende LPUART1 e SPI
  na saída. Depois de desenhar a placa, calcula a tarifa uma vez, mostra
  valor e status e envia P1. UUID e valor ficam fixos nos reenvios.
  Desistir invalida a cobrança antes de CANCEL. Só PAID exato autoriza
  e a sessão permanece ativa até a passagem confirmada.
- **Cenários** O launcher oficial v1.0.1 (`82d854b`) executou os 19
  cenários públicos e aprovou todos, incluindo os quatro do G3 e os
  cenários de pagamento e reset. A análise estática registrou zero
  achados em `student/src`.
- **Cobertura final** Os 19 cenários e as oito suítes ztest/FFF (129
  testes) passaram em `firmware-coverage.py`. Foram cobertas 967/986
  linhas (98,07%) e 679/734 ramos (92,51%), com média de 95,29%.
  Os testes de falha exercitam a mesma lógica de produção com falsos
  apenas nas suítes. A medição não usou exclusões.
- **Rastros** Em `normal` (execução `a949a52d4b5f4c7d8325e0357802bdca`),
  o terminal registrou uma cobrança de 700 centavos e o display
  registrou 7224 pixels escritos sem erros. O verde de saída acendeu
  em 14,920012 s virtuais, com o motor desligado. Em
  `obstruction-during-close` (execução `4fe57a53304e4c13b3864d1720d05f4a`),
  o feixe subiu em 103800000 µs no relógio de origem e o motor foi
  desligado em 103800009 µs. São 9 µs no rastro, dentro do prazo de 10 ms.
- **Evidências** Na cópia oficial de validação, consultar
  `build/sprint-publicos-01/assessment/results.json` e `assessment/runs/`
  da mesma execução, além de `build/sprint-coverage-final/summary.json`
  e `build/sprint-static-01/summary.json`.
- **Para a sabatina** Explicar como a fila distingue perder bytes de
  receber um LF, por que a thread de controle pode continuar enquanto
  o display trabalha e como transação corrente, UUID e sessão ativa
  impedem que um pagamento atrasado abra uma saída nova.

### 2026-11-21 — Validação final e conferência com o plano da dupla

- **Aluno** Matheus.
- **Ferramenta** Claude Code (Claude Opus 5.5).
- **Pedido** Retomar a sessão anterior e rodar a validação completa no
  commit `04edb60`. Conferir se o resultado corresponde ao plano da dupla
  (divisão de tarefas, cronograma, Git e contratos técnicos). Atualizar
  este registro e gerar um roteiro de estudo para a sabatina.
- **Proposta e execução** Rodar o script local de validação (19 cenários,
  `firmware-coverage.py` e `static-check.py`) e comparar o código e o
  histórico Git com o plano. Nenhuma alteração de firmware.
- **Verificação** Os 19 cenários públicos passaram, incluindo os quatro
  do G3. `firmware-coverage.py` aprovou as 8 suítes (129 testes) e mediu
  967/986 linhas (98,07%) e 679/734 ramos (92,51%), média 95,29%, sem
  exclusões. `static-check.py` terminou com zero achados. Os números
  coincidem com a validação de Pablo na v1.0.1 oficial. Também foram
  conferidos no código os contratos citados no plano: tarifa, limite de
  9999 unidades, UUID fixo nos três envios, textos do display e sessão
  ativa só depois da passagem.
- **Divergências em relação ao plano**
  - O plano atribuía a Matheus o pagamento P1, a tarifa e o display. Na
    prática, Pablo implementou essas partes no commit `49845cd`, que
    também alterou `services.c`, `camera.c` e `parking_uart.c`. Matheus
    escreveu câmera UART e sessões.
  - O commit `49845cd` entrou direto em `solucao`, sem PR. A revisão
    cruzada de Matheus sobre essa parte, exigida pelo plano, está pendente.
  - O firmware completo ficou pronto em 21/11, e não em 20/11. O sábado,
    reservado ao estudo, foi usado para a integração.
  - Este checkout não contém a v1.0.1 oficial (`82d854b`) nem o remote
    `upstream`. A validação local depende da alteração local de
    `scripts/lab.py` (`skip-worktree`, não versionada).
- **Aceitação ou alterações** As conclusões aguardam revisão de Matheus e
  Pablo. Próximos passos: Matheus revisar o commit `49845cd` e estudar a
  parte da saída; alinhar o checkout com a v1.0.1 e descartar a alteração
  local de `scripts/lab.py`.
- **Evidências** `build/val-1121-190130/` (`<cenário>/assessment/results.json`,
  `coverage/summary.json` e `static/summary.json`).

### 2026-11-21 — Testes de unidade de `main.c` e `parking_exit.c`

- **Aluno** Matheus.
- **Ferramenta** Claude Code (Claude Opus 5.5).
- **Pedido** Cobrir os ramos de `main.c` (62,50%) e `parking_exit.c`
  (60,00%), os mais fracos da validação final, e validar de novo.
- **Proposta da IA** Os ramos que faltavam eram caminhos de erro que os
  cenários não alcançam. Em vez de excluir linhas, a IA criou duas suítes
  ztest/FFF no padrão de `tests/uart_hw`: incluem o `.c` de produção e
  trocam drivers, lógica das faixas e criação de threads por falsos.
  Nenhum arquivo de `student/src` mudou.
  - `tests/main` (7 testes) renomeia o `main()` do firmware com
    `#define main parking_main`, porque o ztest tem o seu próprio `main()`.
    Verifica: tabela de serviços nula ou incompleta (`-EINVAL`) e registro
    repetido (`-EALREADY`, mantendo a primeira); resposta nula ou de faixa
    inexistente; fila de respostas cheia (`-ENOMSG`, sem bloquear);
    respostas entregues em ordem FIFO à faixa certa; limite de 8 respostas
    por passada; as duas faixas avançam mesmo sem serviços registrados.
  - `tests/exit` (6 testes) verifica: falha ao abrir o terminal impede
    serviços e thread; ligação de `services_exit_io` e prioridade
    preemptiva do worker; placa recusada quando o display falhou na
    inicialização; perda de bytes (`-EOVERFLOW`) ressincroniza o parser
    antes do próximo byte e preserva o horário de chegada; limite de 256
    bytes por passada.
- **Aceitação ou alterações** Matheus pediu a mudança, a validação e o
  commit.
- **Verificação** `validar.sh` aprovou os 19 cenários públicos.
  `firmware-coverage.py` aprovou 13 suítes ztest (142 testes, 0 falhas):
  `main.c` e `parking_exit.c` com 100% de linhas e de ramos. O total foi de
  968/986 linhas (98,17%) e 692/734 ramos (94,28%), média de 96,22%
  (antes 95,29%), sem exclusões. `static-check.py` terminou com zero
  achados.
- **Evidências** `build/cov-glue-01/` (suítes com `partial-startup`) e
  `build/val-1121-201055/` (validação completa).
- **Para a sabatina** Explicar a técnica de incluir o `.c` com `#define`
  para redirecionar chamadas e por que os limites de 8 respostas e de 256
  bytes por passada protegem a segurança e os prazos do pagamento.

### 2026-11-23 — Guia de estudo para a sabatina

- **Aluno** Pablo Munih Silva de Carvalho.
- **Ferramenta** Codex (GPT-6).
- **Pedido** Ler o firmware da dupla e produzir um documento que ensine o
  código em uma ordem que ajude a explicar e alterar qualquer requisito
  na sabatina.
- **Proposta e execução** Elaborar um guia linear do commit `9d1da39`,
  com referências de arquivo e função, um exemplo completo de entrada e
  saída e exercícios de explicação e mudança ao vivo para os 13 requisitos.
- **Aceitação ou alterações** A preparação do guia foi solicitada por Pablo.
  A revisão do documento pelo aluno está pendente.
- **Verificação** Conferência das explicações com o código, os contratos e
  as suítes da dupla. As 17 páginas do PDF foram renderizadas e revisadas,
  e foram conferidos os links de navegação e os 13 exercícios. Os cenários
  e as métricas citados são os da validação de 21/11. Esta tarefa foi
  documental e não executou novamente o firmware.

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| | | |
