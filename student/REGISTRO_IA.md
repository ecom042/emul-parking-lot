# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| `<Preencher Aluno 1>` | `<Matrícula 1>` | `hw`, `adc`, `lane`, `controller`, suíte `tests/unit` (bloco físico/segurança) |
| `<Preencher Aluno 2>` | `<Matrícula 2>` | `camera`, `payment`, `session`, `display`, `font`, suíte `tests/unit` (bloco protocolos/display) |

## Ferramentas

| Aluno                 | Ferramenta                                    | Modelo/versão                   | Período de uso |
| --------------------- | --------------------------------------------- | ------------------------------- | -------------- |
| `Guilherme`           | Antigravity                                   | Claude Opus 4.6 / Gemini Carbon | 2026-11        |
| `Guilherme`           | Cursor                                        | Grok 4.7                        | 2026-11-23     |
| `<Preencher Aluno 2>` | `<Preencher se usou IA ou "Não utilizou IA">` | `<Modelo>`                      | 2026-11        |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-18 — Arquitetura modular e camada fina de hardware (`hw`, `controller`, `main`)

- **Aluno:** `<Preencher Aluno 1>`
- **Requisito(s):** REQ-01, REQ-04, REQ-06, REQ-08, REQ-10, REQ-13
- **O que pedi:** Estruturar o firmware do estacionamento de duas faixas em Zephyr RTOS separando estritamente os drivers reais (`GPIO/EXTI`, `ADC4` assíncrono, `UART`, `SPI`) da lógica determinística de controle para permitir testes de unidade com `ztest` + `FFF`.
- **O que a IA propôs:** Criação de `src/hw.h` e `src/hw.c` contendo apenas repasses diretos de 1 a 4 linhas às APIs devicetree do Zephyr (`GPIO_DT_SPEC_GET`, `SPI_DT_SPEC_GET`, `ADC_CHANNEL_CFG_DT`, `k_work` na workqueue cooperativa do sistema para EXTI), coordenados por `src/controller.c` e `src/main.c`.
- **O que aceitei, mudei ou rejeitei, e por quê:** Rejeitei a primeira versão que usava `CONFIG_UART_INTERRUPT_DRIVEN` com mutação de estado dentro de ISR e `LCOV_EXCL_LINE` acima do teto de 3%. Aceitei a versão refatorada com `uart_poll_in`/`uart_poll_out` não-bloqueante, trabalho diferido via `k_work` nas interrupções EXTI e seção crítica (`irq_lock`) na transição para fechamento.
- **Como verifiquei:** Compilação limpa para `parking_lot_u5` com `ZEPHYR_SCA_VARIANT=gcc` (`-fanalyzer` e todos os 11 avisos de `static-check.py` com 0 achados) e validação dos cenários públicos e suíte `ztest` + `FFF`.
- **O que aprendi / o que ainda preciso entender:** Entendi como o `k_work` na system workqueue do Zephyr permite desligar o motor em microssegundos após uma borda EXTI sem condições de corrida com o laço principal.

### 2026-11-18 — Aquisição assíncrona compartilhada do ADC4 e máquina de estados das faixas (`adc`, `lane`)

- **Aluno:** `<Preencher Aluno 1>`
- **Requisito(s):** REQ-01, REQ-03, REQ-06, REQ-07, REQ-08, REQ-09, REQ-10
- **O que pedi:** Implementar o gerenciador de proprietário único do `ADC4` alternando os canais 17 (entrada) e 18 (saída) e a máquina de estados determinística das duas faixas.
- **O que a IA propôs:** Módulo `src/adc.c` com uma única requisição `adc_read_async` pendente por vez, buffers/sinal estáticos persistentes, verificação de conclusão estritamente antes de `+100 ms` e histerese `< 683` / `> 1024`; módulo `src/lane.c` tratando partida segura (20 ms de acomodação pós-reset), abertura/fechamento em até 2500 ms, passagem por feixe em até 10000 ms, parada de obstrução em `< 10 ms`, pulso de 100 ms no buzzer e distinção entre assistência rearmável (barreira fechada) e falhas travadas até reset.
- **O que aceitei, mudei ou rejeitei, e por quê:** Rejeitei a tentativa inicial de submeter duas leituras assíncronas simultâneas no mesmo `ADC4` (que causava contenção/`-EBUSY`) e adicionei verificação explícita de perda do fim de curso inferior em `IDLE`.
- **Como verifiquei:** Cenários `normal`, `stationary`, `noisy-threshold`, `jam`, `missing-upper`, `missing-lower`, `contradictory-limits`, `partial-startup`, `obstruction-before-close`, `obstruction-during-close` e testes de unidade de falha de driver ADC/GPIO.
- **O que aprendi / o que ainda preciso entender:** Entendi por que o lock de contexto do driver ADC do Zephyr exige alternância explícita dos canais num único dono e por que os buffers precisam sobreviver a conclusões atrasadas.

### 2026-11-18 — Parsers UART limitados, sessões, tarifa congelada e display ST7789V (`camera`, `payment`, `session`, `display`, `font`)

- **Aluno:** `<Preencher Aluno 2>`
- **Requisito(s):** REQ-02, REQ-04, REQ-05, REQ-11, REQ-12, REQ-13
- **O que pedi:** Implementar e revisar os analisadores independentes das câmeras AT (`CRLF`) e do terminal P1 (`LF`), a tabela de sessões, o cálculo inteiro da tarifa e o driver SPI do display `ST7789V`.
- **O que a IA propôs:** `src/camera.c` e `src/payment.c` com buffers limitados a 128 bytes incluindo terminador, prazo de montagem parcial de 1000 ms com descarte até `LF`, rejeição estrita de `CR` no protocolo P1, até 3 capturas com IDs novos e até 3 envios idempotentes com o mesmo UUID, invalidação do UUID antes de `P1 CANCEL`; `src/display.c` com máquina de estados não-bloqueante de inicialização (pulso real no pino `reset` ativo-baixo, `SWRESET`, `SLPOUT`, `COLMOD 0x55`, `MADCTL 0x00`, `DISPON`) e renderização da placa antes do congelamento da tarifa seguida de `BRL r.cc`, `SIMULATED PAYMENT` e `NOT PIX`.
- **O que aceitei, mudei ou rejeitei, e por quê:** Corrigi três defeitos críticos da versão preliminar: (1) `payment.c` ignorava `\r` em vez de descartar o quadro malformado até `\n`; (2) `display_init` bloqueava o boot por 400 ms com `k_msleep` e não gerava borda `HIGH -> LOW` no pino `reset` ativo-baixo do modelo `CourseST7789V`; (3) faltava enviar `P1 CANCEL` ao esgotar as 3 tentativas de pagamento.
- **Como verifiquei:** Cenários `confidence-retry`, `low-confidence`, `camera-timeout`, `payment-delayed`, `payment-missing`, `payment-duplicate`, `payment-stale`, `reset-capture`, `reset-payment`, fila interativa de carros (`driver.py`) e suíte `ztest` + `FFF` cobrindo linhas e branches.
- **O que aprendi / o que ainda preciso entender:** Entendi a diferença entre nível lógico (`GPIO_ACTIVE_LOW`) no Zephyr e nível físico recebido pelo modelo `ST7789V`, além da importância da ordem exata de invalidação do UUID antes do envio de `CANCEL`.

### 2026-11-23 — Compilação no Zephyr 4.5 e prazo do ADC4 durante o desenho (`hw`, `display`, `controller`, `tests/unit`)

- **Aluno:** `<Preencher Aluno 1>`
- **Requisito(s):** REQ-10, REQ-12
- **O que pedi:** Testar o firmware no laboratório v1.0.1, ver se os cenários públicos, a cobertura e a análise estática passavam, e corrigir o que falhasse. Não abrir o pull request.
- **O que a IA propôs:** Trocar `SPI_DT_SPEC_GET` para a forma de dois argumentos do Zephyr 4.5 e `SPI_OP_MODE_MASTER` por `SPI_OP_MODE_CONTROLLER`. Entre cada linha desenhada no ST7789V, devolver o controle ao coordenador para ele ler o ADC4 e repetir a parada de segurança, porque um quadro inteiro passava de 100 ms na compilação de cobertura e travava as duas faixas. Ajustar o teste de unidade para o relógio falso acompanhar o passo, em vez de saltar para 700000000 ms.
- **O que aceitei, mudei ou rejeitei, e por quê:** Aceitei as três mudanças. A v1.0.1 do professor não entra neste arquivo: ela só faz o `lab.py` aceitar a data de build do Renode no Linux, e fica no repositório da disciplina, fora de `student/`.
- **Como verifiquei:** No laboratório v1.0.1, `lab.py --headless normal` (`build/normal-03`, `"failure": null`), `firmware-coverage.py` (`build/cobertura-05`: 19 cenários públicos e a suíte `unit` passaram; 99,18% das linhas e 95,78% dos desvios) e `static-check.py` (`build/estatica-03`: 0 achados).
- **O que aprendi / o que ainda preciso entender:** `<Preencher com as suas palavras: por que um desenho longo no display faz o ADC4 compartilhado estourar o prazo de 100 ms>`

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| `student/CMakeLists.txt`, `student/Kconfig`, `student/prj.conf`, `student/boards/zvb/parking_lot_u5/zvb.yml` | Código inicial fornecido pelo repositório da disciplina (`ecom042/emul-parking-lot`) | Apache-2.0 |
| Uso das macros `GPIO_DT_SPEC_GET`, `SPI_DT_SPEC_GET`, `ADC_CHANNEL_CFG_DT` e `k_poll_signal` em `src/hw.c` | Padrão documentado na API do Zephyr RTOS v4.5.0 e na especificação do laboratório | Apache-2.0 |
