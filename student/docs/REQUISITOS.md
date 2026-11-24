# Requisitos explicados (preparo para a sabatina)

Na sabatina o professor sorteia um REQ e pede duas coisas: (a) os estados, as transições e os
prazos, e onde eles estão no código; (b) uma mudança ao vivo, como um prazo, um limiar ou uma
regra de rearme. Por isso cada seção abaixo traz as mesmas partes: **o que é**, **estados e
transições**, **números**, **onde fica no código** (módulos planejados no `SPEC.md`),
**armadilhas** e **mudança ao vivo provável**.

---

## Visão geral: a máquina de estados de uma faixa

As duas faixas rodam o **mesmo código**, com dados diferentes (`struct lane`). A saída tem um
estado a mais, PAGANDO.

```
            ┌──────────── código > 1024 (abandono) ────────────┐
            ▼                                                   │
 PARTIDA ─► OCIOSA ──código<683 e armada──► IDENTIFICANDO ──placa ok──► [saída] PAGANDO ──PAID──┐
  │          ▲                                  │ 3 falhas                     │ falha/3 envios │
  │          │                                  ▼                              ▼                │
  │          │                             ASSISTÊNCIA ◄───────────────────────┘                │
  │          │                       (rearma: código>1024 e feixe livre)                        │
  │          │                                                                                  ▼
  │          └── fim de curso inferior ◄── FECHANDO ◄── feixe on→off e código>1024 ◄── PASSAGEM ◄── ABRINDO
  │                                            │ obstrução / 2500 ms                        10000 ms │ 2500 ms
  │                                            ▼                                                     ▼
  └─► (sem inferior / limites contraditórios) TRAVADA (só o reset tira) ◄──────── falha de posição ──┘
```

- **ASSISTÊNCIA** é rearmável. Acontece com a cancela fechada: placa ruim, lotado, sem sessão,
  pagamento falhou.
- **TRAVADA** só sai com reset. Acontece com a cancela fora de 0°, com fins de curso
  contraditórios ou com falha do ADC.

Princípio central: **o firmware decide, os modelos só reagem.** O cenário mede pinos, bytes de
UART/SPI e tempos. Mensagem no console não prova nada.

---

## REQ-01 — Partida segura

**O que é.** Ao ligar, o sistema fica num estado seguro e confere se a cancela está mesmo fechada.

**Estados e transições.**
1. `main()` configura todas as saídas **inativas** e depois acende o vermelho (motor off, verde off).
2. Lê os fins de curso de cada faixa:
   - **inferior = 1 e superior = 0** → OCIOSA (normal);
   - **inferior = 0** (cancela parcialmente aberta, cenário `partial-startup`) → assistência.
     **Não** tentar fechar sozinho ("homing" é proibido: alguém pode estar embaixo do braço);
   - **inferior = 1 e superior = 1** (impossível fisicamente) → TRAVADA até reset. Continua
     travada **mesmo que o cenário depois "conserte" a falha**.
3. A regra de contraditórios vale **o tempo todo**, não só na partida: os dois fins de curso
   ativos em qualquer momento → TRAVADA.

**Onde (no código entregue).** `main.c` (`main`: `lane_init` → `hw_init` → `lane_start` →
`display_init` → laço); `lane.c` (`lane_start` acende o vermelho; `boot_check` confere os fins
de curso **depois de `STARTUP_SETTLE_MS` = 20 ms**, porque logo após o boot as entradas ainda
não refletem os modelos); `barrier.c` (`barrier_start`; contraditórios a qualquer momento em
`barrier_safety`). Teste: `tests/lane` (`test_boot_waits_before_checking_limits`).

**Armadilhas.** Configurar o GPIO como `GPIO_OUTPUT_ACTIVE` faz o motor dar um pulso; use
`GPIO_OUTPUT_INACTIVE`. Ler os limites antes de configurar as interrupções (EXTI).

**Mudança ao vivo provável.** "Na partida sem inferior, em vez de assistência rearmável, trave
até o reset", ou o contrário.

---

## REQ-02 — Reconhecimento da placa

**O que é.** O carro chega perto e o firmware pede à câmera a placa, com até 3 tentativas.

**Estados e transições.**
- OCIOSA → IDENTIFICANDO quando a **cancela está fechada** (inferior ativo), a **amostra do ADC
  é fresca** (nova, desde a última decisão) com código **< 683**, e a faixa está **armada**
  (ver REQ-03).
- Ao entrar no estado: `tentativa = 1`, `id = ++contador`. Envia `AT+CAPTURE=<id>\r\n` e marca
  `prazo = agora + 1000 ms`.
- Respostas:
  - `OK\r\n` → só confirma o pedido; **não** reinicia o prazo e **não** é a placa;
  - `+PLATE:<id>,"<placa>",<conf>\r\n` com **id igual ao atual**, placa válida e
    **conf ≥ 85** → placa aceita. Segue para REQ-04 (sessões);
  - conf < 85, ou prazo vencido → próxima tentativa com **ID novo** e prazo novo;
  - id diferente, linha malformada ou maior que 128 bytes → descarta e continua esperando a
    mesma tentativa.
- Na 3ª falha → ASSISTÊNCIA.

**Números.** Limiar 683 (≈1000 mm, porque `682 = 1000×4095/6000`). Prazo de 1000 ms. Máximo de
3 tentativas. Confiança mínima de 85. Placa `[A-Z]{3}[0-9][A-Z0-9][0-9]{2}` (ex.: `ABC1D23`,
`ABC1234`). ID canônico de 1 a 999999999, sem zero à esquerda.

**Por que ID novo?** Uma resposta atrasada da tentativa 1 pode chegar durante a tentativa 2.
Com ID novo ela é reconhecida como velha e descartada. Isso é **correlação**.

**Cenários.** `normal` (93 de primeira), `confidence-retry` (62 → 79 → 93: abre na 3ª),
`low-confidence` (3 baixas → assistência), `camera-timeout` (sem resposta → assistência).

**Onde (no código entregue).** `camera.c` (`camera_format_capture`, `camera_parse`,
`camera_plate_valid`); `lane.c` (`start_capture`, `next_attempt`, `camera_service`, caso
`LANE_IDENTIFY` de `state_step`). Constantes `CAMERA_*` em `config.h`.

**Mudança ao vivo provável.** Trocar 3 tentativas por 2, a confiança 85 por 90, ou o prazo de
1000 ms por 1500 ms. Deixe tudo em `#define` ou Kconfig com nome claro.

---

## REQ-03 — Presença, não passagem

**O que é.** "Estar perto" não é "chegar de novo". Um carro parado na frente da cancela não pode
gerar capturas sem fim, e o ruído não pode gerar chegadas falsas.

**Mecanismo: histerese com dois limiares e a flag `armada`.**
- **< 683** = presente. **> 1024** = ausente (≈1500 mm). **Entre 683 e 1024: nada muda.**
- A faixa só dispara captura se `armada == true`. Ao disparar, `armada = false`. Ela só volta a
  `true` quando aparece uma amostra > 1024 (o carro foi embora).
- Ruído de ±64 em torno de 683 oscila entre 619 e 747, mas nunca passa de 1024. Por isso não
  rearma (`noisy-threshold`).
- Carro parado (`stationary`): uma captura, uma abertura, e mais nada durante 9 s.

**Abandono.** Em IDENTIFICANDO ou PAGANDO, uma amostra fresca > 1024 significa que o carro foi
embora. O firmware cancela a tentativa:
- invalida o ID de captura e o UUID, para que respostas que chegarem depois sejam ignoradas;
- na saída, envia `P1 CANCEL <uuid>\n`;
- volta para OCIOSA, já armada.

**Onde (no código entregue).** `lane.c`: `lane_step` atualiza `dist`/`dist_fresh` e liga
`armed` com código > `DIST_ABSENT_ABOVE`; o caso `LANE_IDLE` de `state_step` exige `armed`,
amostra fresca e código < `DIST_PRESENT_BELOW`; abandono no caso `LANE_IDENTIFY` e em
`paying_step`. Constantes em `config.h`.

**Mudança ao vivo provável.** Mudar o limiar de presença (ex.: 800 mm → código 546) ou a folga
da histerese. Lembre da conversão `código = mm × 4095 / 6000`.

---

## REQ-04 — Sessões e faixas simultâneas

**O que é.** A tabela de sessões (placa + instante da entrada) é **compartilhada** pelas duas
faixas, com no máximo `CONFIG_PARKING_CAPACITY` linhas (1..8, padrão 4).

**Regras.**
- **Entrada, placa aceita:**
  - sem sessão e com vaga → abre;
  - já tem sessão (duplicada) → assistência;
  - lotado → assistência.
- **Saída, placa aceita:** com sessão → PAGANDO (REQ-05); sem sessão (leitura errada da câmera)
  → assistência. Nunca cobrar outro carro.
- A sessão é **criada só quando a passagem de entrada termina** (REQ-07), não quando a cancela
  abre. Ela é **removida só quando a passagem de saída termina**, não quando o pagamento é
  confirmado.
- **Simultaneidade:** uma faixa em assistência ou esperando pagamento não pode atrasar a parada
  do motor da outra. É o motivo de nada no laço poder bloquear.

**O avaliador** confere cada verde contra a tabela vista nas duas faixas e acusa
`capacity_exceeded`, `duplicate_entry` ou `exit_without_session`.

**Detalhe de corrida.** Duas entradas não acontecem ao mesmo tempo (só existe uma faixa de
entrada). Mas: decidir abrir com 3/4 vagas ocupadas, e só criar a sessão na passagem. Como só a
entrada cria sessões, a contagem não fica inconsistente. Se quiser ser rigoroso, conte também as
"reservas" de entradas em andamento.

**Onde (no código entregue).** `sessions.c`; decisão em `plate_accepted` (`lane.c`); a sessão
nasce/morre em `passage_step`.

**Mudança ao vivo provável.** "Lotado: em vez de assistência, apenas não abrir e rearmar", ou
mudar a capacidade.

---

## REQ-05 — Pagamento (só na saída)

**O que é.** A cobrança é idempotente: retransmitir não cobra duas vezes, e só a confirmação
exata abre.

**Estados e transições em PAGANDO.**
1. Placa com sessão. Desenha a placa no display e **congela a tarifa** (REQ-12). Desenha o valor,
   `SIMULATED PAYMENT` e `NOT PIX`.
2. Gera um **UUID novo** e envia `P1 CHARGE <uuid> <placa> <centavos> BRL\n`. Marca
   `envios = 1` e `prazo = agora + 1000 ms`.
3. Respostas:
   - `P1 RESULT <uuid atual> PAID <mesmo valor> BRL\n` → ABRINDO;
   - `CANCELLED`, UUID antigo, valor diferente, malformada ou `P1 ERROR ...` → **nunca abre**.
     Ignore e espere o prazo;
   - prazo vencido e `envios < 3` → **reenvia a mesma linha** (mesmo UUID, mesmo valor) e
     `envios++`;
   - prazo vencido e `envios == 3` → invalida o UUID, envia `P1 CANCEL <uuid>\n` e vai para
     ASSISTÊNCIA.
4. Carro saiu (> 1024) → abandono, CANCEL e OCIOSA (REQ-03).

**Por que o mesmo UUID no reenvio?** Se o terminal cobrou mas a resposta se perdeu, um UUID novo
seria uma **segunda cobrança**. Com o mesmo UUID o terminal reconhece a repetição e responde de
novo o mesmo resultado.

**Cenários.**
- `payment-delayed`: resposta atrasada; reenvia e abre só quando chega a resposta certa.
- `payment-missing`: nunca responde; 3 envios e assistência.
- `payment-duplicate`: um PAID repetido depois não pode reabrir a cancela.
- `payment-stale`: o PAID do carro anterior (UUID velho) chega enquanto outro carro paga. Não
  pode abrir.

**Onde (no código entregue).** `payment.c` (`payment_format_charge/cancel`, `payment_parse`,
`payment_new_uuid`); `lane.c` (`start_payment`, `send_charge` reenvia **a mesma linha**
guardada em `charge_line`, `payment_service`, `paying_step`, `cancel_charge` apaga o UUID
**antes** de mandar o CANCEL). Decisão: o carro sair durante o pagamento também invalida,
cancela e pede assistência (o contrato diz "on timeout, departure or cancellation ... then
request assistance"); como o carro já está longe, a faixa rearma no passo seguinte.

**Mudança ao vivo provável.** Trocar 3 envios por 2, ou o prazo de 1000 ms por 2000 ms.

---

## REQ-06 — Movimento da cancela

**O que é.** O motor só para quando o **fim de curso real** avisa, e cada movimento tem prazo.

**Estados (`barrier.c`).** FECHADA → ABRINDO → ABERTA → FECHANDO → FECHADA, além de FALHA.
- **Abrir:** primeiro `direção = 1`, depois `enable = 1`. Marca `prazo = agora + 2500 ms`.
- **EXTI do superior** → callback (contexto de interrupção) → `k_work_submit` → a work faz
  `enable = 0`. Só **depois** de motor off **e** superior ativo é que acende o verde.
- **Fechar:** `direção = 0`, `enable = 1`, até o EXTI do inferior. Desliga o motor.
- O laço principal confere o prazo de 2500 ms. Se venceu, desliga o motor e vai para TRAVADA
  (posição parcial).

**Números.** 45°/s, então 0° → 90° leva 2 s, com 500 ms de folga até o prazo. O avaliador aceita
até 2510 ms (tolerância de um tick). Trocar a direção custa 100 ms parado; isso ainda cabe no
prazo.

**Por que o modelo não para o motor?** No mundo real, o motor ligado contra o batente queima. A
responsabilidade é do firmware, e o cenário `jam` prova isso.

**Falhas.**
- `jam`: o braço não se mexe. O prazo de 2500 ms desliga o motor e vai para assistência, sem
  verde.
- `missing-upper`: o braço chega a 90° mas o fim de curso nunca acende. Prazo e assistência.
  **Não** acenda o verde "porque já deu tempo".
- `missing-lower`: ao fechar, o inferior nunca acende. Prazo e assistência; não inicia nova
  identificação.

**Onde (no código entregue).** `barrier.c` (`motor_start`, `barrier_safety`, `barrier_step`);
`hw.c` (`input_isr` → `k_work` → `on_input` em `main.c` → `lane_on_input`). **O prazo de
2500 ms também roda fora do laço:** `motor_start` chama `hw_wake_after`, que agenda uma
`k_work_delayable` para o instante do prazo; ela chama `barrier_safety`, que compara
`hw_now_ms()` com `deadline_ms` e desliga o motor. Motivo: desenhar a tela de cobrança leva
~40 ms e não pode atrasar a parada da outra faixa (tolerância de 10 ms).

**Mudança ao vivo provável.** Mudar o prazo de 2500 ms (ex.: para 3000 ms).

---

## REQ-07 — Passagem

**O que é.** Abrir não é passar. Só a evidência física de que o carro atravessou conta.

**Estado PASSAGEM**, ao entrar em ABERTA com o verde aceso, com `prazo = agora + 10000 ms`:
1. espera o feixe **ativo** (`viu_feixe = true`);
2. depois espera o feixe **inativo** **e** uma amostra fresca com código **> 1024** (o carro já
   está a mais de 1500 mm do outro lado);
3. então: na entrada, `sessions_add(placa, agora)`; na saída, `sessions_remove(placa)`. Vai para
   FECHANDO.
- Prazo de 10000 ms vencido → ASSISTÊNCIA. A cancela está aberta (posição de falha), então
  **não fecha sozinha**.
- **Passagem tardia.** Se o carro termina a passagem depois do prazo (parado no feixe e depois
  segue), a sessão ainda é criada (entrada) ou encerrada (saída). A faixa continua travada e a
  cancela parada. Motivo: o avaliador conta as sessões pela passagem física; sem isso, a
  lotação do firmware ficaria diferente da real e um carro a mais poderia entrar
  (`capacity_exceeded`), ou um carro que está dentro não conseguiria sair. Achado com o
  simulador de fila (`assess.py --lot`, perfil `linger_in_beam`).

**Por que as duas condições?** O feixe sozinho pode ser um pedestre. A distância sozinha pode
ser o carro dando ré.

**Onde (no código entregue).** `lane.c`, `passage_step` e `passage_done` (e o caso
`LANE_OPENING` de `state_step`, que acende o verde e marca o prazo); a passagem tardia em
`late_passage_step`, chamada no estado LOCKED, com a marca `passage_late` posta em `assist`.

**Mudança ao vivo provável.** Mudar o prazo de 10000 ms, ou exigir o feixe inativo por N ms.

---

## REQ-08 — Fechamento e obstrução

**O que é.** Fechar com segurança.

**Sequência.**
1. Verde off, vermelho on (**antes** de mexer no motor).
2. Confere o feixe: se estiver ativo, **não** começa a fechar. O avaliador reprova fechar com o
   feixe já ativo.
3. `direção = 0`, `enable = 1`, prazo de 2500 ms.
4. **Feixe ativo durante o fechamento** → o EXTI do feixe vai para a work, que desliga o motor
   **em até 10 ms**. Vai para TRAVADA (posição parcial): **sem reverter, sem religar**, mesmo que
   o feixe libere depois.

**Por que não reverter?** Reverter sozinho com algo embaixo do braço é ação automática insegura;
quem decide é um humano (reset).

**Cenários.** `obstruction-before-close` (feixe preso: a cancela fica aberta, motor off; vence o
prazo de passagem e vai para assistência) e `obstruction-during-close` (para na hora, não volta).
Este segundo é **portão G3**.

**10 ms.** O laço de 10 ms sozinho pode estourar o limite. Por isso a parada vai pela interrupção
do feixe, e o laço confere de novo como segunda proteção.

**Onde (no código entregue).** `barrier_close` (recusa com o feixe ativo), `barrier_safety`
(obstrução em `BARRIER_MOTION_CLOSING`), `passage_step` (verde off / vermelho on antes de
`barrier_close`).

**Mudança ao vivo provável.** "Após a obstrução, rearme quando o feixe liberar", que seria uma
mudança de regra de rearme. Saiba explicar por que o original é mais seguro.

---

## REQ-09 — Assistência

**O que é.** O "pedido de ajuda" único para tudo o que sai do normal.

**Ações (sempre as mesmas):** motor off, verde off, vermelho on, buzzer em 1 por **100 ms** e
depois em 0. O desligamento do buzzer é feito pelo laço com prazo, sem `k_sleep(100)`.

**Dois tipos.**

| Tipo | Quando | Como sai |
|---|---|---|
| Rearmável (cancela fechada) | placa ruim, 3 tentativas, lotado, duplicada, sem sessão, pagamento falhou | amostra fresca > 1024 **e** feixe livre → OCIOSA, armada |
| Travada | posição parcial (jam, sem fim de curso, obstrução no fechamento, prazo de passagem com a cancela aberta), contraditórios, falha do ADC | só reset |

**Onde (no código entregue).** `lane.c`, função `assist(l, latched, now)`; rearme no caso
`LANE_ASSIST` de `state_step`; buzzer desligado por `buzzer_service`.

**Mudança ao vivo provável.** Mudar a duração do pulso (100 → 200 ms) ou a regra de rearme
(ex.: exigir > 1024 por 500 ms).

---

## REQ-10 — ADC4 assíncrono

**O que é.** Um só conversor para dois canais, sem nunca bloquear o laço.

**Mecanismo (`adc_sampler.c`).**
- Buffer, `adc_sequence` e `k_poll_signal` são **`static`**. Se fossem variáveis locais, uma
  conversão concluída tarde escreveria numa pilha que já foi reaproveitada.
- Estados: LIVRE → PENDENTE (canal X, `t_envio`) → LIVRE.
- Em cada tick:
  - se PENDENTE, `k_poll_signal_check()` (não bloqueia):
    - terminou com sucesso → guarda a amostra da faixa X com a marca `fresca` e passa ao
      outro canal;
    - não terminou e `agora ≥ t_envio + 100 ms` → **falha travada** nas duas faixas
      (assistência até reset). **Não reenvia**: o pedido ainda é do driver;
  - se LIVRE, `k_poll_signal_reset()` e `adc_read_async()` no próximo canal (17 ↔ 18).
- O resultado, um erro do envio ou uma amostra inválida também viram falha travada.
- "Fresca" quer dizer que a decisão consome a amostra (`fresca = false` depois de usar). Assim
  nenhuma transição usa valor velho.

**Por que alternar?** Só um pedido por vez. Com tick de 10 ms, cada faixa recebe uma amostra a
cada ~20 ms, bem dentro do sensor (atualiza a cada 10 ms).

**Onde (no código entregue).** `adc_sampler.c` (`adc_sampler_step`, `adc_sampler_take`);
`hw.c` (`hw_adc_start`, `hw_adc_poll`, com `adc_buffer`, `adc_seq` e `adc_signal` estáticos).

**Mudança ao vivo provável.** Mudar o prazo de 100 ms, ou a ordem dos canais.

---

## REQ-11 — UARTs robustas

**O que é.** O analisador de linhas (`line_parser.c`) aguenta lixo, fragmentos e ataques. Há uma
instância por UART: câmera de entrada, câmera de saída e terminal.

**Regras.**
- Os bytes chegam em pedaços de até 8, um a cada 10 ms. O analisador **monta a linha** num buffer
  de 128 bytes.
- Linha completa (LF) → entrega para o interpretador do protocolo. Na câmera, exige o CR antes do
  LF; no P1, **proíbe** o CR.
- Passou de 128 bytes sem LF → modo **descarte até o próximo LF**. **Não** interpreta o começo
  ("nunca executar prefixo").
- Linha começada há mais de 1000 ms sem terminar → descarta o que tem e o resto até o LF.
- Interpretação estrita: sem espaços extras, sem campos vazios, números canônicos (sem zero à
  esquerda, dentro da faixa). Qualquer desvio → descarta.

**Por que "até o LF"?** Se eu descartar 128 bytes e recomeçar no meio da linha, o resto dela pode
parecer um comando válido.

**Onde (no código entregue).** `line_parser.c` (`lp_feed`, `finish`, `drop`,
`lp_parse_uint`); recepção por interrupção em `hw.c` (`uart_isr` → `ring_buf`), consumo em
`camera_service`/`payment_service` (`lane.c`).

**Mudança ao vivo provável.** Mudar o tamanho máximo (128 → 64) ou o prazo de montagem.

---

## REQ-12 — Display e tarifa

**Display ST7789V (`display.c`)** via SPI1, com CS ativo baixo, D/C (0 = comando, 1 = dados) e
reset ativo baixo.
- Inicialização: RESET baixo 10 ms → alto, espera 120 ms → `01` SWRESET, 150 ms → `11` SLPOUT,
  120 ms → `3A 55` (RGB565) → `36 00` → `29` DISPON.
- Desenhar um retângulo: `2A` CASET (x0, x1 em 16 bits), `2B` RASET (y0, y1), `2C` RAMWR + pixels
  RGB565, byte alto primeiro.
- O texto usa uma fonte bitmap própria: cada caractere vira um bloco de pixels.
- A tela de cobrança mostra a placa, `BRL 4.00`, `SIMULATED PAYMENT` e `NOT PIX`.

**Tarifa (`tariff.c`).**
```
unidades = max(1, ceil(estadia_ms / unidade_ms))     // ceil inteiro: (t + u - 1) / u
centavos = 200 + (unidades - 1) * 100
```
- A estadia conta a partir da **passagem de entrada**.
- O valor é **congelado na primeira cobrança**: depois de desenhar a placa, antes de desenhar o
  valor e enviar o CHARGE. Reenvios usam o mesmo valor.
- Mais de 9999 unidades → assistência (o terminal aceita no máximo 1000000 centavos).
- Exemplos com unidade de 60000 ms: 60000 ms → 200; 60001 ms → 300.
- Com unidade de 1000 ms (cenários): 2500 ms → 3 unidades → 400 → `BRL 4.00`.
- Formatação: `BRL %u.%02u` com `centavos / 100` e `centavos % 100`.

**Mudança ao vivo provável.** Mudar a fórmula (ex.: primeira unidade 300) ou o limite de
unidades.

---

## REQ-13 — Reset

**O que é.** O reset da máquina no Renode reinicia o firmware do zero.

**O que garantir.**
- Sessões, contadores de ID, UUIDs, prazos e buffers dos analisadores: tudo começa vazio (são
  variáveis estáticas zeradas no boot).
- Uma resposta pendente de antes do reset (`reset-capture`, `reset-payment`) **não autoriza
  nada**. O firmware novo não tem pedido pendente, então qualquer `+PLATE` ou `PAID` que chegar
  não corresponde a nada e é descartado.
- As saídas voltam ao estado seguro do REQ-01.

**Cuidado com o UUID.** Se o gerador for determinístico (mesma semente a cada boot), o 1º UUID
depois do reset é igual ao 1º de antes. O laboratório limpa o cache do terminal no reset, mas é
mais robusto misturar um valor que varia (contador de ciclos no momento do pedido, ruído do ADC).
Saiba explicar a escolha.

**Onde (no código entregue).** Tudo é estático e zerado no boot (`lane_init`,
`adc_sampler_init`, `sessions_clear` em `main`). O UUID mistura `hw_entropy()` (ciclos da CPU)
e um contador (`payment_new_uuid`).

---

## Mapa de prazos e limiares (cole na parede)

| Constante | Valor | Requisito |
|---|---|---|
| Presença | código < 683 (~1000 mm) | 02, 03 |
| Ausência / abandono / rearme | código > 1024 (~1500 mm) | 03, 07, 09 |
| Resposta da câmera | 1000 ms, 3 tentativas, conf ≥ 85 | 02 |
| Resposta do pagamento | 1000 ms, 3 envios no total | 05 |
| Abrir / fechar | 2500 ms (tolerância 2510) | 06 |
| Passagem | 10000 ms | 07 |
| Parada por obstrução | ≤ 10 ms | 08 |
| Pulso do buzzer | 100 ms | 09 |
| Conversão do ADC | < 100 ms | 10 |
| Quadro de UART / montagem | 128 B / 1000 ms | 11 |
| Tarifa | 200 + (u−1)×100; u ≤ 9999 | 12 |
| Tick do laço | 10 ms | todos |
| Acomodação na partida | 20 ms | 01 |
