# Para o Plácido: o que o branch `feat/entry-lane` faz

Escrito pelo Kauã, branch `feat/entry-lane`. Complementa o
[`contrato-e-divisao.md`](contrato-e-divisao.md) e o
[`para-kaua.md`](para-kaua.md). O estado geral está em
[`contexts/estado-atual.md`](contexts/estado-atual.md), e cada passo, com o que
aprendi, está no [`REGISTRO_IA.md`](../REGISTRO_IA.md).

## 1. Resumo

- **A faixa de entrada funciona de ponta a ponta** no Renode, com os seus
  serviços `camera` e `distance` ligados.
- **Cenários: 13 de 19 passam, incluindo os 4 do G3** (`obstruction-during-close`,
  `jam`, `contradictory-limits`, `missing-lower`). Os 6 que faltam são de
  saída e pagamento (M3).
- **Cobertura:** 99,89% de linhas e 95,64% de branches; `static-check.py`
  com 0 achados.
- **8 commits**, um por passo verificado (`git log solucao..feat/entry-lane`).

## 2. Mudei 3 arquivos seus: revise, por favor

Só uma linha em cada um, em `camera_proto.c`, `p1_proto.c` (`finish`) e
`text.c` (`text_format_brl`):

```c
/* antes */  return (n < 0 || (size_t)n >= size) ? -ENOSPC : n;
/* depois */ return ((size_t)n >= size) ? -ENOSPC : n;
```

**Por quê:** um `n` negativo convertido para `size_t` vira um número enorme,
então `>= size` já devolve `-ENOSPC`. O `n < 0 ||` era um branch que nada
alcançava. Ficou um comentário explicando no código. O comportamento não
muda; os seus testes continuam passando.

Também acrescentei testes nas suas suítes (`wire`, `camera_proto`,
`p1_proto`, `adc4`) e uma suíte nova de erros de `hw/` (seção 5).

## 3. Como os seus serviços são usados

| Serviço | Onde é chamado | O que eu assumo dele |
|---|---|---|
| `camera_init` | `runner_init()`, nas duas faixas | — |
| `camera_start` | ação de entrada do IDENTIFYING | o retorno é ignorado: todo caminho para fora do IDENTIFYING termina ou abandona a captura |
| `camera_abandon` | ações de entrada de IDLE, ASSIST e ASSIST_LOCKED | o motor da máquina não tem ação de saída, então a limpeza vai nos estados de destino |
| `camera_poll` | `runner_step()`, a cada volta (~1 ms) | — |
| `camera_plate` | guarda `plate_may_enter` **e** ação do CLOSING (cria a sessão) | **a placa continua disponível até o próximo `camera_start`**, inclusive depois de `camera_abandon` |
| `distance_init` | `runner_init()`, uma vez, depois das faixas | se falhar, as duas faixas vão para a assistência e não recebem `EV_BOOT` |
| `distance_start` | `runner_start()`, depois do `EV_BOOT` | — |
| `chan_distance` | listener do `services/presence` | só reage a publicações, nunca lê o valor inicial 0 |

**Uma pergunta para você:** a placa mantida até o próximo `camera_start` é
um comportamento que você garante? Hoje o código faz isso (`camera_abandon`
não apaga `has_plate`), mas não está escrito no `camera.h`. Se concordar,
vale uma linha no Doxygen do `camera_plate`.

`payment` e `screen` ainda não são chamados; entram no M3.

## 4. O que é novo (do meu lado)

| Módulo | Para que serve |
|---|---|
| `fsm/states` | a máquina da entrada: INIT, IDLE, IDENTIFYING, OPENING, OPEN, PASSING, CLOSING, ASSIST, ASSIST_LOCKED (diagrama no `estado-atual.md`) |
| `fsm/runner` | partida (`runner_init`), threads (`runner_start`) e uma volta da thread (`runner_step`); uma faixa com falha de inicialização fica em assistência |
| `fsm/lane` | prazos por estado: `deadline_ms[]` com 0, um orçamento ou `LANE_DEADLINE_KEEP`; `EV_TIMEOUT` é aplicado direto, sem passar pela fila |
| `services/presence` | histerese 683/1024 sobre `chan_distance` (começa em "desconhecido") e o feixe (`EV_BEAM_*` na ISR) |
| `services/sessions` | tabela de vagas com `k_mutex` |
| `main.c` | só `runner_init()` e `runner_start()` |

**Decisões que mudaram o combinado:**
- **Sessões com `k_mutex`, não zbus** (`chan_sessions` do diagrama 06). As
  faixas só consultam quando precisam, e ninguém precisa ser avisado de uma
  mudança na tabela.
- **A saída ainda não tem thread nem regras.** Ela só configura o
  equipamento e fica com o motor parado e o vermelho aceso.

## 5. Testes: o que mudou na estrutura

- **Suíte nova `tests/fsm/`:** `states` e `runner` com falsos do FFF de
  todos os serviços, em `fakes/service_fakes.{h,c}`. É separada porque em
  `tests/unit` os serviços são compilados de verdade.
- **Asserts testados:** `CONFIG_ZTEST_ASSERT_HOOK` em `tests/unit` e
  `tests/hw`. `tests/unit/asserts/` chama `lanes_get`, `lane_hw_get` e
  `camera_start` com uma faixa inválida; o teste passa quando o assert
  dispara.
- **`tests/hw/errors/`:** um dispositivo de teste cujo init falha
  (`DEVICE_DEFINE`) faz `indicator`, `sensor`, `uart_line` e `adc4`
  devolverem `-ENODEV`.
- **Cuidado com o FFF:** ele guarda o **ponteiro** dos argumentos. Para
  verificar uma string que vem de um buffer local, use um falso que copia o
  valor (exemplo em `tests/fsm/states/test_states.c`, `record_asked_plate`).

## 6. O que ainda não está coberto e é seu

- **`st7789.c`: 17 branches** (a cadeia de erros da inicialização SPI). Dá
  para cobrir com um driver SPI falso só de teste, no mesmo esquema do
  dispositivo quebrado. Você topa fazer, ou prefere que eu faça?
- **`camera.c:54` e `payment.c:57`:** `__ASSERT_NO_MSG(len > 0)` depois de
  formatar num buffer interno que sempre cabe. Nenhuma entrada da API faz
  falhar. Manter (2 branches sem cobertura, justificados) ou trocar por um
  `BUILD_ASSERT` do tamanho do buffer?

## 7. Como revisar

1. `git log --oneline solucao..feat/entry-lane`: um commit por passo, na
   ordem em que dá para entender.
2. Leia, nesta ordem: `fsm/lane.h` (prazos), `fsm/states.c` (a tabela é o
   comportamento inteiro), `fsm/runner.c`, `services/presence.c`,
   `services/sessions.c`.
3. Rode e confira:

```sh
. ../ambiente.sh
python scripts/firmware-coverage.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/coverage-placido --jobs 4
python scripts/static-check.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/static-placido
```

Lembre que na sabatina qualquer um de nós pode ter de explicar ou mudar
este código. Se alguma linha não estiver clara, comente no PR que eu
explico.

## 8. Próximo: M3 (saída)

Eu faço `services/tariff` e a tabela e a thread da saída, na ordem já
combinada: `screen_show_plate` → `tariff_cents` → `screen_show_amount` →
`payment_start`, com `EV_TARIFF_OUT_OF_RANGE`, `payment_poll` na volta da
thread e `payment_cancel` ao desistir. Vou usar os seus `payment` e `screen`
como estão; aviso se precisar mudar algum header.
