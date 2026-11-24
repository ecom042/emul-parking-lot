# Guia de estudo: controlador de estacionamento

Este guia explica o firmware de `student/` para a sabatina. Para cada requisito
ele mostra os estados, as transições, os prazos e **onde isso está no código**,
e sugere uma mudança ao vivo para treinar. Leia com o código aberto ao lado.

> Ordem sugerida: §1 (visão geral) → §2 (a máquina de estados) → §3 (os
> requisitos, um por um) → §5 (treino de mudanças ao vivo).

---

## 1. Visão geral

### 1.1 Módulos

| Arquivo | O que faz | Toca no hardware? | Testado em |
|---|---|---|---|
| `src/parking.h` | **Todas as constantes** (limiares, prazos, tentativas) | não | — |
| `src/lane.c/.h` | Máquina de estados de **uma** faixa (usada 2 vezes) | não (chama `hw.h`) | `tests/faixa` |
| `src/adc_sampler.c/.h` | Dono único do ADC4: 1 conversão por vez, alterna canais, prazo 100 ms | não (chama `hw.h`) | `tests/logica` |
| `src/line_parser.c/.h` | Monta linhas de UART (128 B, 1000 ms, descarte até LF) | não | `tests/logica` |
| `src/protocol.c/.h` | Formata/valida AT (câmera) e P1 (terminal), placa, UUID | não | `tests/logica` |
| `src/tariff.c/.h` | `200 + (u−1)×100`, assistência acima de 9999 unidades | não | `tests/logica` |
| `src/sessions.c/.h` | Tabela de placas estacionadas, capacidade configurável | não | `tests/logica` |
| `src/display.c/.h`, `src/font.c/.h` | ST7789V: inicialização e tela de cobrança; fonte 5x7 própria | não (chama `hw.h`) | `tests/logica` |
| `src/hw.c/.h` | **Única** camada que chama drivers do Zephyr | **sim** | cenários |
| `src/main.c` | Laço de controle + thread do display | sim (via `hw.h`) | cenários |
| `src/ui.h` | Interface laço → thread do display | — | falsos em `tests/faixa` |

A ideia central: **lógica separada do hardware.** `lane.c` nunca chama
`gpio_pin_set_dt`; chama `hw_motor()`. Nos testes, `hw_motor()` é um falso do
FFF que registra a chamada e pode devolver `-EIO`. Assim os caminhos de erro
são testados sem Renode.

### 1.2 Threads e interrupções

```
 EXTI (fins de curso, feixe) ─┐
 RX das UARTs (ring buffer) ──┼─> k_sem_give(wake_sem) ──> LAÇO DE CONTROLE (main, prioridade 2)
 thread do display (pronta) ──┘                             a cada volta (≤ 2 ms):
                                                            1. lane_tick(entrada), lane_tick(saída)
 ADC4 (k_poll_signal) <── verificado sem bloquear ───────── 2. UARTs -> line_parser -> lane_on_*_line
                                                            3. adc_sampler_poll -> lane_on_distance
                                                            4. display pronto? -> lane_on_display_done
                                                            5. hw_wait_event(2 ms)

 THREAD DO DISPLAY (prioridade 10, mais baixa): k_msgq -> display_show_plate/amount (SPI bloqueante)
```

- **Por que as ISRs só acordam o laço?** Quem decide é a máquina de estados,
  em contexto de thread. O laço tem prioridade alta e acorda em microssegundos
  de tempo virtual, bem abaixo dos 10 ms da obstrução (REQ-08).
- **Por que o display tem thread própria?** Desenhar milhares de bytes por SPI
  bloqueia. Se o laço desenhasse, uma obstrução na **outra** faixa poderia
  esperar o SPI terminar (REQ-04). Com a thread de prioridade baixa, o laço
  interrompe o desenho sempre que precisa.
- **Tempo:** tudo usa `k_uptime_get()` (`hw_now_ms`), que no Renode é tempo
  virtual (regra 4 do desafio).

---

## 2. A máquina de estados (`lane.c`)

```
                 +-------------------- (saída) --------------------+
                 |                                                  v
 STARTUP -> IDLE -> IDENTIFYING -> SHOW_PLATE -> SHOW_AMOUNT -> PAYING
              ^        |  (entrada)                                 |
              |        +------------------> OPENING <---- PAID -----+
              |                                |  (fim de curso superior)
              +-------- CLOSING <---- OPEN <---+

 Qualquer estado --(falha)--> ASSIST  (rearmável ou travada)
```

| Estado | Saídas | Sai quando… | Prazo |
|---|---|---|---|
| `STARTUP` | motor off, vermelho | 50 ms depois do boot: fechada → `IDLE`; senão → assistência travada | `STARTUP_SETTLE_MS` |
| `IDLE` | vermelho | código < 683 → `IDENTIFYING` | — |
| `IDENTIFYING` | vermelho | `+PLATE` válido ≥ 85 → autoriza; < 85 ou prazo → nova tentativa; 3 falhas → assistência; código > 1024 → `IDLE` | 1000 ms por tentativa |
| `SHOW_PLATE` / `SHOW_AMOUNT` | vermelho | thread do display terminou | 3000 ms |
| `PAYING` | vermelho | `PAID` exato → `OPENING`; prazo → reenvia (máx. 3); `CANCELLED`/saiu → assistência | 1000 ms por envio |
| `OPENING` | motor sobe | superior → motor off → verde → `OPEN` | 2500 ms |
| `OPEN` | verde | feixe ativo, depois livre + código > 1024 → sessão criada/encerrada → `CLOSING` | 10000 ms |
| `CLOSING` | vermelho, motor desce | inferior → motor off → `IDLE`; feixe → assistência travada | 2500 ms |
| `ASSIST` | motor off, vermelho, buzzer 100 ms | rearmável: código > 1024, feixe livre, fechada → `IDLE`; travada: só reset | — |

**Quatro funções de entrada** (todas em `lane.c`):

- `lane_tick()`: chamada em toda volta. Lê fins de curso e feixe, aplica
  **todos os prazos** e desliga o buzzer. É onde ficam as regras de segurança.
- `lane_on_distance()`: só com amostra **nova** do ADC. Chegada, abandono,
  passagem e rearme dependem da distância, então só acontecem aqui.
- `lane_on_camera_line()` / `lane_on_payment_line()`: linhas completas e já
  montadas.
- `lane_on_display_done()`: fim de uma etapa do desenho.

**Funções auxiliares importantes:** `assist()` (entra em assistência),
`start_motor()` (liga o motor + prazo de 2500 ms), `start_attempt()` (nova
captura), `authorize()` (decide abrir/cobrar/recusar), `send_charge()` e
`cancel_payment()`.

---

## 3. Requisito por requisito

### REQ-01 Partida segura
- **Onde:** `lane_init()` (saídas seguras), `lane_tick()` caso `LANE_STARTUP`,
  e o teste `if (upper && lower)` no começo de `lane_tick()`.
- **Por que esperar 50 ms (`STARTUP_SETTLE_MS`)?** Depois do reset, o modelo só
  volta a dirigir as entradas na primeira amostra de 10 ms
  (docs/physical-model.md, "Sensor settling"). Se lêssemos na hora, o inferior
  ainda estaria baixo e pediríamos assistência à toa.
- **Sem inferior** (braço a meio caminho): assistência **travada**, motor
  parado. Nunca "voltamos para casa" sozinhos.
- **Superior e inferior juntos:** `assist(l, true, "contradictory limits")`.
  `latched = true` nunca volta a `false`, então mesmo que a falha suma a faixa
  continua parada até o reset.
- **Cenários:** `partial-startup`, `missing-lower`, `contradictory-limits`.

### REQ-02 Reconhecimento da placa
- **Onde:** `lane_on_distance()` caso `LANE_IDLE` (chegada), `start_attempt()`,
  `attempt_failed()`, `lane_on_camera_line()`; validação em
  `camera_parse_plate()` (protocol.c).
- **ID novo por tentativa:** `next_capture_id` (estático em lane.c, compartilhado
  pelas duas faixas). Uma resposta com ID diferente de `l->capture_id` é
  ignorada.
- **`OK` e `ERROR`** não passam em `camera_parse_plate()`: são ignorados e não
  reiniciam o prazo (o prazo só muda em `start_attempt()`).
- **Prazo:** `l->deadline_ms = now + CAMERA_ATTEMPT_MS`, conferido em
  `lane_tick()` → `attempt_failed()`.
- **Confiança:** `r.confidence < CAMERA_MIN_CONFIDENCE` → próxima tentativa
  **na hora** (85 é aceito).
- **3 tentativas esgotadas** → `assist(l, false, ...)`, rearmável.
- **Cenários:** `normal`, `confidence-retry`, `low-confidence`, `camera-timeout`.

### REQ-03 Presença, não passagem
- **Histerese:** chegada só com `< 683`; saída só com `> 1024`. Entre os dois,
  nada muda. Por isso o ruído de ±64 em volta de 682 (`noisy-threshold`) não
  gera chegadas repetidas: depois da primeira captura o estado já não é `IDLE`.
- **Carro parado** (`stationary`): depois de aberto, a faixa está em `OPEN` e a
  distância só importa para a passagem. Nada de nova captura.
- **Depois de uma assistência rearmável**, o carro parado continua presente
  (`< 1024`), então não há nova tentativa (o rearme exige `> 1024`).
- **Abandono:** em `IDENTIFYING`, `> 1024` → volta a `IDLE` **sem**
  assistência (decisão nossa: o carro só foi embora). Em `SHOW_*`/`PAYING`,
  `> 1024` → assistência + `P1 CANCEL` (o contrato manda pedir assistência
  quando o carro sai durante o pagamento).

### REQ-04 Sessões e faixas simultâneas
- **Onde:** `authorize()` (decisão), `sessions.c` (tabela).
- **Entrada:** placa já estacionada → assistência; lotado
  (`!sessions_has_space()`) → assistência; senão abre.
- **Saída:** placa sem sessão → assistência (nunca cobra outra pessoa).
- **A sessão só nasce/morre na passagem completa** (`lane_on_distance()` caso
  `LANE_OPEN`), nunca ao abrir nem ao pagar.
- **Simultaneidade:** uma única thread atende as duas faixas sem nunca
  bloquear; o que bloqueia (SPI) está na thread do display.

### REQ-05 Pagamento
- **Onde:** `lane_on_display_done()` (gera UUID e primeiro envio),
  `send_charge()`, `lane_tick()` caso `LANE_PAYING` (reenvio),
  `lane_on_payment_line()`, `cancel_payment()`.
- **UUID:** `uuid_make(seq, now)`, com `seq` um contador que só cresce desde o
  reset. Cada tentativa de saída tem um UUID novo. Os reenvios usam
  **o mesmo** UUID e o mesmo `l->amount`.
- **Só abre com:** estado `PAYING` **e** UUID igual ao atual **e** `PAID`
  **e** valor igual. Qualquer outra coisa é ignorada.
- **Depois do `PAID`:** `uuid_active = false` e o estado vira `OPENING`, então um
  `PAID` duplicado (`payment-duplicate`) não faz nada.
- **Desistir:** `cancel_payment()` **primeiro invalida** (`uuid_active = false`)
  e **depois** manda `P1 CANCEL`. A ordem importa: mesmo que um `PAID` chegue
  entre as duas coisas, ele já não vale.
- **`payment-stale`:** o terminal reenvia o `PAID` do carro anterior (UUID
  antigo) enquanto a nova cobrança está pendente. `strcmp(r.uuid, l->uuid) != 0`
  → ignorado; o prazo expira e vem a assistência.

### REQ-06 Movimento da cancela
- **Onde:** `start_motor()` (liga + prazo), `lane_tick()` casos `LANE_OPENING`
  e `LANE_CLOSING`.
- **Para pelo fim de curso real:** o motor só desliga quando `upper` (ou
  `lower`) é lido em nível alto. O modelo **nunca** desliga o motor.
- **Ordem no topo:** `hw_motor(OFF)` → só se deu certo → `hw_lights(verde)`.
- **Prazo de 2500 ms:** travamento (`jam`) ou fim de curso ausente
  (`missing-upper`/`missing-lower`) → assistência **travada** (posição
  desconhecida).
- **`hw_motor()` em hw.c:** direção primeiro, enable depois.

### REQ-07 Passagem
- **Onde:** `lane_tick()` caso `LANE_OPEN` (marca `beam_seen`, prazo de 10 s) e
  `lane_on_distance()` caso `LANE_OPEN` (completa a passagem).
- **Condição:** `beam_seen && code > 1024 && !hw_beam()`, ou seja, o feixe ficou
  ativo, agora está livre e a amostra nova mostra o carro longe.
- **Prazo de 10 s** sem passagem completa → assistência travada (a cancela
  está aberta: não fechamos sozinhos). É o caso de `obstruction-before-close`.

### REQ-08 Fechamento e obstrução
- **Onde:** `start_close()` e `lane_tick()` caso `LANE_CLOSING`.
- **Antes de fechar:** `hw_lights(vermelho)` e só então `MOTOR_CLOSE`. O feixe
  está livre por construção (a passagem exige feixe livre).
- **Obstrução:** feixe ativo em `CLOSING` → `assist(..., true, ...)`, que
  desliga o motor. A EXTI do feixe acorda o laço na hora. Nunca reverte nem
  religa: a assistência é travada e o rearme ignora faixas travadas.

### REQ-09 Assistência
- **Onde:** `assist()`.
- **Sempre:** motor off, verde off, vermelho on. O buzzer só pulsa na
  **primeira** entrada (`if (l->state != LANE_ASSIST)`), e o fim do pulso é
  conferido em `lane_tick()` (100 ms).
- **Rearmável × travada:** o parâmetro `latch`. Rearme em
  `lane_on_distance()` caso `LANE_ASSIST`: `!latched && code > 1024 && feixe
  livre && fechada`.

| Causa | Tipo |
|---|---|
| câmera esgotada, lotado, placa repetida, saída sem sessão | rearmável |
| pagamento: prazo, cancelado, carro saiu, display falhou, tarifa > 9999 | rearmável |
| sem inferior na partida, contraditório, prazo do motor, obstrução, passagem, perdeu fim de curso, falha de GPIO do motor, falha do ADC | **travada** |

### REQ-10 ADC4 assíncrono
- **Onde:** `adc_sampler.c` (lógica) e `hw_adc_start()`/`hw_adc_check()` (hw.c).
- **Um dono, uma conversão por vez:** `s.pending`. Enquanto pendente, nunca
  dispara outra. Quando termina, entrega a amostra e dispara **o outro** canal
  (alterna 17/18).
- **Sem bloquear:** `k_poll_signal_check()` só olha o sinal.
- **Prazo de 100 ms:** se `now − submitted_ms ≥ 100` e ainda pendente → falha
  travada. Erro do driver ou código > 4095 também.
- **Buffers que sobrevivem:** `adc_buffer`, `adc_seq` e `adc_signal` são
  `static` em hw.c. O driver guarda ponteiros para eles até concluir, mesmo
  depois do nosso prazo; se estivessem na pilha, uma conclusão atrasada
  escreveria em memória inválida.
- **Depois da falha:** `adc_sampler_poll()` retorna sempre `false` (não dispara
  nada), e `main.c` chama `lane_fault()` nas duas faixas uma única vez.

### REQ-11 UARTs robustas
- **Onde:** `line_parser.c`; uma instância por UART em `main.c` (`parsers[]`).
- **128 bytes contando o terminador**, só ASCII imprimível; câmera exige CR LF
  (o CR só pode ser o penúltimo byte), terminal proíbe CR.
- **Descarte até o LF:** qualquer erro liga `discarding` ou `bad`. A linha só é
  entregue se chegou inteira e boa, então nunca se executa um prefixo.
- **Prazo de montagem:** `line_parser_poll()` → mais de 1000 ms desde o primeiro
  byte → descarta até o próximo LF.
- **Recepção:** a ISR (`uart_isr` em hw.c) copia os bytes para um `ring_buf`
  por UART; o laço lê em `service_uart()`.

### REQ-12 Display e tarifa
- **Ordem exigida:** desenhar a placa → **congelar a tarifa** → desenhar valor e
  avisos → mandar `CHARGE`. Está em `authorize()` (pede a placa) e
  `lane_on_display_done()` (calcula `l->amount` uma vez, pede o valor, depois
  envia).
- **Tarifa:** `tariff_amount()`, com `units = ceil(elapsed/unit)` (mínimo 1),
  `> 9999` → `-ERANGE` → assistência.
- **Display:** `display_init()` faz o reset por hardware (10 ms, 120 ms),
  SWRESET (150 ms), SLPOUT (120 ms), COLMOD 55, MADCTL 00, DISPON e pinta a tela
  de preto. `display_text_band()` desenha uma faixa de texto centralizado:
  monta uma linha de 240 pixels RGB565 por vez e manda por SPI depois de
  CASET/RASET/RAMWR.
- **Fonte:** `font.c`, 5x7, desenhada para este trabalho.

### REQ-13 Reset
- O reset da máquina recarrega o ELF, e todo o estado (sessões, IDs, UUIDs,
  parsers) está em variáveis estáticas reinicializadas em `main()`. Uma
  resposta atrasada de antes do reset não é recebida (o modelo também zera), e
  mesmo que fosse, o ID ou UUID não bateria com o da nova execução.

---

## 4. Como rodar e medir

```sh
. ~/estacionamento/ambiente.sh && cd "$LAB"
# um cenário
python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
  --output build/normal-01 --headless normal
# todos os cenários e as suítes + cobertura (5-10 min)
python scripts/firmware-coverage.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/cobertura-01
# análise estática
python scripts/static-check.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/estatica-01
```

**Resultados na entrega desta versão** (tempo virtual, unidade de tarifa
1000 ms):

| Medida | Resultado |
|---|---|
| Cenários públicos | 19/19 |
| Testes de unidade | `logica` 42 testes, `faixa` 46 testes, todos passando |
| Cobertura | 100% linhas, 98,8% branches |
| Análise estática | 0 achados |
| Fila de carros (`assess.py --lot`) | todos os veredictos certos |

**Os 5 desfechos de branch não cobertos** (você pode ser perguntado):
- `lane.c`: os três `if (n > 0)` depois de formatar um quadro (buffer de 128 B
  sempre basta; é defesa) e a volta do ID de captura a 1 depois de 999999999.
- `hw.c`: `if (signaled == 0U)` em `hw_adc_check()` (o compilador gera um
  desfecho extra; as duas linhas são executadas).

**As 12 exclusões `LCOV_EXCL_LINE`** (limite: 3%): todas são falhas de driver
que o Renode nunca produz (`device_is_ready` falso, `hw_init` falhando, falha
do ADC, falha do SPI do display). A lógica dessas falhas é testada nas suítes
de unidade com FFF (por exemplo `test_erro_do_driver_na_conclusao`,
`test_falha_do_adc_trava`, `test_display_falha_ou_demora`).

---

## 5. Treino para a mudança ao vivo

Para cada mudança: altere, rode o cenário indicado e confira que ele passa (ou
que falha do jeito que você previu).

| Pedido provável | Onde mudar | Cenário para conferir |
|---|---|---|
| "O prazo da câmera passa a ser 800 ms" | `CAMERA_ATTEMPT_MS` em parking.h | `camera-timeout`, `confidence-retry` |
| "Aceite confiança ≥ 90" | `CAMERA_MIN_CONFIDENCE` | `confidence-retry` (93 ainda passa), `low-confidence` |
| "Só 2 tentativas de captura" | `CAMERA_MAX_ATTEMPTS` | `low-confidence` espera `captures: 3`, então **vai falhar**: explique por quê |
| "Prazo do motor de 2200 ms" | `MOTOR_BUDGET_MS` | `jam`, `missing-upper` (abrir leva 2000 ms: ainda passa) |
| "Pulso do buzzer de 200 ms" | `BUZZER_PULSE_MS` | `jam` (`buzzer_count` continua 1) |
| "Limiar de chegada em 600" | `DIST_ARRIVE_BELOW` | `normal` (carro a 800 mm = código 546: ainda chega) |
| "Rearme só com o carro a mais de 2000 mm" | `lane_on_distance()` caso `LANE_ASSIST`: troque `DIST_LEAVE_ABOVE` por uma constante nova (2000 mm ≈ código 1365) | `low-confidence`, `camera-timeout` |
| "Abandono na identificação também pede assistência" | `lane_on_distance()` caso `LANE_IDENTIFYING`: troque `enter(l, LANE_IDLE…)` por `assist(l, false, "left", now_ms)` | `tests/faixa`: `test_carro_sai_durante_identificacao` vai falhar (atualize-o) |
| "Máximo de 2 envios da cobrança" | `PAYMENT_MAX_SENDS` | `payment-missing` (assistência mais cedo), `payment-delayed` (`PAID` chega a ~1,6 s: ainda passa com 2) |
| "Tarifa base 300" | `TARIFF_BASE_CENTAVOS` em tariff.h | `tests/logica` `test_tariff` vai falhar: atualize os valores esperados |

**Perguntas de entendimento para responder sozinho:**
1. Por que o verde acende só depois de `hw_motor(OFF)` retornar 0?
2. O que acontece se um `PAID` correto chegar depois do terceiro prazo?
3. Por que `adc_buffer` não pode ser variável local de `hw_adc_start()`?
4. Por que o rearme exige o feixe livre, além da distância?
5. O que impede um carro parado a 800 mm de gerar capturas infinitas?
6. Se o display demorasse 5 s, o que aconteceria com a faixa de entrada?
   E com a de saída?
7. Por que a passagem é verificada em `lane_on_distance()` e não em `lane_tick()`?
