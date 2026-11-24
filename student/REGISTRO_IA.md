# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| Erick de Lima Mascarenhas | 23211622 | Interfaces dos módulos (`hw.h`, `line.h`, `payment.h`, `tariff.h`, `display.h`), montador de quadros UART (`line.c`) e sua suíte `tests/line`, camada de hardware (`hw.c`), ADC4 (`distance.c`), câmera (`camera.c`), sessões (`session.c`), máquina de estados das faixas (`lane.c`), `main.c` e as suítes `tests/camera`, `tests/session`, `tests/distance`, `tests/lane` e `tests/hw` |
| João Victor Cavalcante da Silva Correia | 2021301012 | Tarifa (`tariff.c`), pagamento P1 (`payment.c`), display ST7789V com fonte própria (`display.c`), as constantes e funções acrescentadas a `tariff.h`, `payment.h` e `display.h`, e as suítes `tests/tariff`, `tests/payment` e `tests/display` |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| Erick de Lima Mascarenhas | Claude Code | Claude Opus 5.5 (`claude-opus-5-5`) | 2026-11-18 em diante |
| João Victor Cavalcante da Silva Correia | Claude Code (extensão do VS Code) | Claude Opus 5.5 (`claude-opus-5-5`) | 2026-11-20 em diante |
| Erick de Lima Mascarenhas | OpenAI Codex | GPT-6 | 2026-11-24 |

## Tarefas

### 2026-11-24 — Revisão de segurança e correções antes da entrega

- **Aluno:** Erick de Lima Mascarenhas
- **Requisito(s):** REQ-01 a REQ-03, REQ-05 a REQ-12.
- **O que pedi:** revisar o firmware contra o contrato do laboratório e corrigir localmente os problemas reproduzidos na revisão, antes da entrega.
- **O que a IA propôs:** verificar os fins de curso em todos os estados que dependem deles, dar precedência aos prazos vencidos, confirmar a chegada perto do limiar, conferir o feixe imediatamente antes de iniciar o fechamento e comandar as saídas seguras de forma independente. No P1, verificar a partida do carro antes de consumir o resultado e aguardar distância recente antes de permitir que um PAID abra a cancela. No display, aguardar as duas conclusões de SPI sem bloquear as faixas.
- **O que aceitei, mudei ou rejeitei, e por quê:** a pedido do aluno, o Codex aplicou as correções com mudanças restritas a `student/`. Os modelos e cenários públicos da disciplina permaneceram iguais.
- **Como a IA verificou:** recompilação, análise estática do SDK e cenários locais com o Renode. A revisão do aluno antes da entrega é independente desses resultados.
- **O que ficou para explicar na sabatina:** ordem entre o SPI e o CHARGE, retenção de um PAID durante amostra vencida, instantes dos prazos e limites do filtro perto de 1000 mm.

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-18 — Ambiente do laboratório no WSL

- **Aluno:** Erick de Lima Mascarenhas
- **Requisito(s):** nenhum (ambiente)
- **O que pedi:** montar o ambiente do guia de instalação no WSL 2 (Ubuntu 24.04, x86_64) e rodar o primeiro teste.
- **O que a IA propôs:** seguir o `docs/instalacao.md` e conferir o SHA-256 do Renode com o `docs/setup.md`. O `lab.py` recusava o Renode oficial x86_64 ("Wrong runtime identity", data de build 20260907 contra 20260906). A IA tentou um script intermediário que reescrevia a data; não autorizei contornar a verificação.
- **O que aceitei, mudei ou rejeitei, e por quê:** rejeitei qualquer contorno. Reportei o problema ao professor, que o corrigiu na v1.0.1; mesclei a v1.0.1 no branch `solucao`.
- **Como verifiquei:** cenário `normal` com o código inicial falhou com a mensagem esperada pelo guia (`checkpoint at 5.90 s (tick 590): position_mdeg expected 90000, observed 0`).
- **O que aprendi / o que ainda preciso entender:** o laboratório confere a identidade do Renode antes de compilar; os prazos são em tempo virtual.

### 2026-11-20 — Interfaces e montador de quadros UART

- **Aluno:** Erick de Lima Mascarenhas
- **Requisito(s):** REQ-11
- **O que pedi:** dividir o projeto em módulos com interfaces fixas, para a dupla trabalhar em paralelo, e implementar a montagem de quadros que os dois analisadores UART usam.
- **O que a IA propôs:** camada fina de hardware (`hw.h`) como único ponto que chama drivers; interfaces de pagamento, tarifa e display para o João; e `line.c`: quadro de até 128 bytes com terminador, só ASCII imprimível, CR LF na câmera e só LF no pagamento, prazo de 1000 ms desde o primeiro byte e descarte até o LF, sem entregar prefixo.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a divisão por módulo (não por faixa, como o desafio recomenda) e o montador como está.
- **Como verifiquei:** suíte `tests/line` passando no Renode via `scripts/firmware-coverage.py`; `line.c` com 100% das linhas e 97,37% dos branches.
- **O que aprendi / o que ainda preciso entender:** por que o CR só pode vir imediatamente antes do LF e por que o espaço do LF é reservado ao guardar cada byte.

### 2026-11-20 — Camada de hardware, ADC4, câmera, sessões e máquina de estados

- **Aluno:** Erick de Lima Mascarenhas
- **Requisito(s):** REQ-01, REQ-02, REQ-03, REQ-06, REQ-07, REQ-08, REQ-09, REQ-10, REQ-13
- **O que pedi:** implementar a parte do controlador que não depende do pagamento: drivers, ADC, câmera, sessões e as duas faixas, priorizando os cenários de segurança.
- **O que a IA propôs:** `hw.c` como único ponto com drivers (GPIO/EXTI com a borda adiada para a work queue, ADC4 assíncrono, UART por polling, SPI do display); `distance.c` como dono único do ADC4, alternando os canais 17 e 18, com prazo de 100 ms e falha travada; `camera.c` com o pedido `AT+CAPTURE` e a validação do `+PLATE`; `session.c` com a tabela por placa; `lane.c` com uma máquina de estados para as duas faixas (BOOT, IDLE, IDENTIFYING, PAYING, OPENING, OPEN, PASSING, CLOSING, ASSIST, FAULT); `main.c` com laço de 1 ms e mutex compartilhado com a work queue.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. A IA trocou a recepção UART por interrupção pela leitura por polling, porque a documentação do laboratório (`hardware-models.md`) não garante transporte UART por IRQ no modelo. Separei a assistência que rearma (cancela fechada) da falha travada (cancela fora do fim de curso, limites contraditórios, prazos do motor, ADC), como pede o REQ-08.
- **Como verifiquei:** 13 cenários públicos que não dependem do pagamento passando (`camera-timeout`, `confidence-retry`, `contradictory-limits`, `jam`, `low-confidence`, `missing-lower`, `missing-upper`, `noisy-threshold`, `obstruction-before-close`, `obstruction-during-close`, `partial-startup`, `reset-capture`, `stationary`); `normal` correto até a entrada, falhando só na saída por falta do pagamento; `scripts/static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** por que a borda do fim de curso é tratada na work queue e não na ISR; por que a falha do ADC trava as duas faixas; por que a faixa espera 20 ms no boot antes de ler os sensores.

### 2026-11-20 — Suítes de unidade e cobertura

- **Aluno:** Erick de Lima Mascarenhas
- **Requisito(s):** REQ-02, REQ-03, REQ-06, REQ-07, REQ-08, REQ-09, REQ-10, REQ-11
- **O que pedi:** testes de unidade com FFF para os meus módulos, cobrindo os caminhos de erro que os cenários não alcançam.
- **O que a IA propôs:** uma suíte por módulo. `lane` usa falsos do FFF de `hw.h`, `distance.h`, `payment.h`, `tariff.h` e `display.h` lendo um "mundo" simulado, com câmera, sessões e montador reais; `distance` usa falsos do ADC; `hw` roda com os drivers e modelos reais. Para reduzir branches que só um driver com defeito alcançaria, `hw_init()` passou a acumular o primeiro erro (`first_error`) em vez de testar cada passo; `distance_init()` e `lane_init()` zeram o estado, o que as suítes usam entre testes. A suíte de exemplo `tests/unit` foi removida.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. As seis linhas excluídas (`LCOV_EXCL`, 1,4%) são falhas de driver e de inicialização que o modelo não produz, cada uma com o motivo no código.
- **Como verifiquei:** `scripts/firmware-coverage.py` com as seis suítes e os 13 cenários que não dependem do pagamento passando: meus arquivos com 100% das linhas; branches com 100%, exceto `hw.c` (84,62%, quatro desvios de erro de driver). `scripts/static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** como o FFF substitui uma função na ligação; por que um teste que roda vários casos num laço precisa restaurar o "mundo" a cada iteração.

### 2026-11-20 — Ambiente do laboratório no Linux

- **Aluno:** João Victor Cavalcante da Silva Correia
- **Requisito(s):** nenhum (ambiente)
- **O que pedi:** montar o ambiente do guia de instalação a partir do repositório privado da dupla, no branch `solucao`.
- **O que a IA propôs:** seguir o `docs/instalacao.md` no Linux Mint 22.3 (base Ubuntu 24.04, x86_64), com o workspace em `/dados/ic/embarcados/Projeto_final/estacionamento` (o disco com mais espaço) e o link `~/estacionamento` para ele; Renode 1.17.0 portátil oficial com o SHA-256 conferido contra o `docs/setup.md`; o `ambiente.sh` com o caminho real, para o CMake não ver dois caminhos para a mesma pasta.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei, inclusive deixar o workspace no disco `/dados`, que tem mais espaço livre que a home.
- **Como verifiquei:** `renode --version` mostrou `1.17.0+20260907gitf1dd1b4af`; o primeiro teste (`normal`) compilou tudo e falhou no link só por falta de `tariff_compute`, `payment_*` e `display_*`, a minha parte, porque o Erick já tinha integrado a máquina de estados.
- **O que aprendi / o que ainda preciso entender:** o `lab.py` confere a identidade do Renode; os prazos são todos em tempo virtual.

### 2026-11-20 — Tarifa

- **Aluno:** João Victor Cavalcante da Silva Correia
- **Requisito(s):** REQ-12
- **O que pedi:** implementar `tariff_compute()` conforme `tariff.h`, só com aritmética inteira, e testar os limites.
- **O que a IA propôs:** unidades = `elapsed / unit + (elapsed % unit != 0)`, que é o teto sem somar `unit - 1` (essa soma estouraria com `elapsed` perto de `INT64_MAX`); mínimo de 1 unidade; `-ERANGE` acima de `TARIFF_MAX_UNITS`; `-EINVAL` para tempo negativo, unidade 0 ou ponteiro nulo. Os preços 200 e 100 viraram `TARIFF_BASE_CENTAVOS` e `TARIFF_EXTRA_CENTAVOS` no `tariff.h`.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei o teto calculado com divisão e resto, que não estoura, e as duas constantes no `tariff.h`, que deixam uma mudança na fórmula num lugar só.
- **Como verifiquei:** suíte `tests/tariff` (5 testes) passando no Renode: 0, 1000, 1001 e 2500 ms com unidade de 1000 dão 200, 200, 300 e 400; 60000 e 60001 ms com unidade de 60000 dão 200 e 300; 9999 unidades dão 1000000 e 10000 unidades dão `-ERANGE`. `scripts/firmware-coverage.py` (19 cenários e 9 suítes passando): `tariff.c` com 100% das linhas e dos branches.
- **O que aprendi / o que ainda preciso entender:** por que o valor é congelado no `lane.c`, antes do `CHARGE`, e nunca recalculado no reenvio.

### 2026-11-20 — Pagamento P1

- **Aluno:** João Victor Cavalcante da Silva Correia
- **Requisito(s):** REQ-05, REQ-11, REQ-13
- **O que pedi:** implementar `payment.c` conforme `payment.h` e a seção "Payment P1" do `docs/device-protocols.md`, com a suíte de testes.
- **O que a IA propôs:** os quadros `CHARGE`, `CANCEL` e as respostas `PAID`/`CANCELLED` esperadas são montados uma vez em `payment_start()`. O reenvio manda os mesmos bytes, então UUID e valor não podem mudar. Uma resposta só vale se for idêntica, caractere a caractere, à esperada (`strcmp`), o que já rejeita UUID antigo, outro valor, zero à esquerda, espaço duplo, `PAID broken BRL` e `P1 ERROR`. UUID `00000000-0000-4000-8000-<contador em 12 hex>`, com o contador fora do estado que `payment_init()` zera, para não repetir desde o boot. A desistência invalida o UUID antes de mandar o `CANCEL`. `payment_stats_get()` expõe contadores de diagnóstico (quadros descartados, vencidos, `CANCEL` que falhou), para não descartar o retorno de `line_expire()` nem o erro do `CANCEL` no fim dos envios.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei a proposta depois da explicação.
- **Como verifiquei:** suíte `tests/payment` (18 testes) passando no Renode: caminho feliz, PAID em fragmentos de 2 bytes, reenvio idêntico até 3 envios, timeout com `CANCEL`, `CANCELLED`, UUID antigo, outro valor, 12 quadros malformados, PAID duplicado, abort seguido de PAID atrasado, quadro parcial vencido e os erros de UART. Cenários `normal`, `payment-delayed`, `payment-missing`, `payment-duplicate`, `payment-stale` e `reset-payment` passando. `scripts/firmware-coverage.py`: `payment.c` com 100% das linhas e 97,67% dos branches. O único desfecho não coberto é o `-ENOSPC` de texto truncado, inalcançável porque placa e valor são validados antes; ficou sem `LCOV_EXCL`. `scripts/static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** por que invalidar antes de cancelar fecha a janela para um PAID atrasado.

### 2026-11-20 — Display ST7789V

- **Aluno:** João Victor Cavalcante da Silva Correia
- **Requisito(s):** REQ-12
- **O que pedi:** implementar `display.c` conforme `display.h`, com uma thread de prioridade menor que a `main`, fila de pedidos e fonte própria.
- **O que a IA propôs:** thread `K_PRIO_PREEMPT(5)`, com um `BUILD_ASSERT` de que fica abaixo da `main`, e uma `k_msgq` de 8 pedidos. As funções públicas validam e enfileiram com `K_NO_WAIT`. A thread faz a inicialização (reset 10 ms, espera 120 ms, SWRESET e 150 ms, SLPOUT e 120 ms, COLMOD 0x55, MADCTL 0x00, DISPON) e depois chama `display_handle()` para cada pedido. Um erro marca o display como falho, e as funções passam a devolver `-EIO`, o que leva a saída à assistência. A tela tem quatro linhas de texto fixas; cada uma é uma janela CASET/RASET/RAMWR com o texto centralizado, enviada linha de pixel por linha de pixel num buffer de 480 bytes. Um `BUILD_ASSERT` prova que as janelas cabem em 240×320. `display_format_amount()` e `display_render_row()` são puras e testadas sem thread.
- **O que aceitei, mudei ou rejeitei, e por quê:** aceitei. Fica para combinar com o Erick: como as funções só enfileiram, o `CHARGE` sai pela UART antes de o SPI terminar a tela; o valor já está congelado no `lane.c`, então o preço não muda.
- **Como verifiquei:** suíte `tests/display` (14 testes) passando no Renode, com um painel falso que confere cada janela, a contagem de bytes e os pixels acesos de cada texto. No cenário `normal`, o modelo do display terminou com `initialized: true`, `errors: 0`, `dropped_commands: 0` e 28608 pixels escritos, exatamente as duas telas. `scripts/firmware-coverage.py`: `display.c` com 99,19% das linhas e 98,82% dos branches. O único ponto não coberto é a saída do laço da thread, que só ocorreria se `k_msgq_get(..., K_FOREVER)` falhasse; ficou sem `LCOV_EXCL`. Total do projeto: 99,84% das linhas e 98,65% dos branches. `scripts/static-check.py` com 0 achados.
- **O que aprendi / o que ainda preciso entender:** por que o RAMWR pode vir numa transação SPI separada do comando (o modelo delimita por CS, não por comando).

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| `src/display.c`, tabela da fonte 5×7 | Própria: desenhada pela IA (Claude Opus 5.5) para este projeto, sem copiar outra fonte (registro do João) | Apache-2.0, a do projeto |
