# Spec: Controlador de estacionamento com duas faixas (parking_lot_u5)

> Fonte: `desafio.pdf` + contratos técnicos de `github.com/ecom042/emul-parking-lot`
> (`docs/student-specification.md`, `docs/device-protocols.md`, `docs/hardware-models.md`,
> `docs/scenarios.md`, `docs/grading-tools.md`). Em divergência: o contrato técnico vale
> para o comportamento, `desafio.md` vale para a nota.

## Objetivo

Firmware Zephyr (v4.5.0) para a placa simulada `parking_lot_u5` (Renode 1.17.0) que opera
**ao mesmo tempo** uma faixa de entrada e uma de saída, com até `CONFIG_PARKING_CAPACITY`
(1..8, padrão 4) sessões ativas. O firmware comanda cancela (motor + direção), sinais e
buzzer por GPIO, lê fins de curso e feixe por EXTI, mede distância pelo ADC4 assíncrono,
conversa com 2 câmeras (AT/CRLF) e 1 terminal de pagamento (P1/LF) por UART e desenha um
ST7789V por SPI. Só conta o que os periféricos fizeram (modelos Renode), nunca o console.

Sucesso = todos os cenários públicos (19) e ocultos passando, gates G1–G4 verdes,
cobertura linhas/branches ≥ 95%, zero achados na análise estática, e os dois alunos
capazes de explicar e alterar qualquer requisito sem agente (sabatina, 30 pts).

### Nota (o que otimizar)

| Item | Pts | Alvo |
|---|---|---|
| Gates G1 build, G2 sem leituras fabricadas, G3 segurança (`obstruction-during-close`, `jam`, `contradictory-limits`, `missing-lower`), G4 `REGISTRO_IA.md` | — | todos obrigatórios, senão "não avaliado" |
| Cenários públicos | 20 | 19/19 |
| Cenários ocultos | 20 | cumprir requisitos, não cenários (inclui as 2 faixas simultâneas) |
| Cobertura (10×linhas% + 10×branches%) | 20 | ≥ 95% / ≥ 90% |
| Análise estática (`-fanalyzer`) | 10 | 0 achados |
| Sabatina individual | 30 | cada um explica e muda qualquer REQ |

## Requisitos funcionais (REQ-01..13, condensados)

| ID | Regra verificável | Cenários públicos |
|---|---|---|
| REQ-01 Partida | Motor off, vermelho on, verde off. Sem fim de curso inferior em repouso → assistência (nunca "homing"). Superior+inferior juntos → assistência travada até reset (mesmo se a falha sumir). | partial-startup, missing-lower, contradictory-limits |
| REQ-02 Placa | Cancela fechada (inferior) + amostra ADC **fresca** < 683 → `AT+CAPTURE=<id>`. Aceita só `+PLATE` do ID atual, placa `[A-Z]{3}[0-9][A-Z0-9][0-9]{2}`, confiança ≥ 85. Até 3 tentativas, ID novo cada, 1000 ms por resposta completa; `OK` não reinicia prazo. Esgotou → assistência. | normal, confidence-retry, low-confidence, camera-timeout |
| REQ-03 Presença | Carro parado não gera nova captura. Histerese 683/1024 absorve ruído ±64. Código > 1024 durante identificação/pagamento abandona e invalida o pedido. | stationary, noisy-threshold |
| REQ-04 Sessões | Entrada: placa sem sessão e com vaga → abre; lotado ou já estacionado → assistência. Saída: só placa com sessão paga e sai; outra → assistência. Faixas independentes; falha/espera de uma não atrasa a segurança da outra. | normal |
| REQ-05 Pagamento | `P1 CHARGE <uuid> <placa> <centavos> BRL\n`. UUID novo por tentativa de saída (36 chars, minúsculo 8-4-4-4-12). Sem resposta em 1000 ms → reenvia igual; máx. 3 envios. Só `PAID` com UUID atual **e** mesmo valor abre. CANCELLED/antigo/malformado/divergente nunca abre. Desistir: invalidar UUID e enviar `P1 CANCEL <uuid>\n`. | payment-delayed, payment-missing, payment-duplicate, payment-stale |
| REQ-06 Cancela | Abre (dir=1, en=1) até EXTI do superior; fecha (dir=0) até inferior. Parada pelo fim de curso real (callback EXTI → work), nunca pelo batente. 2500 ms por movimento (tolerância 10 ms). Verde só com superior ativo **e** motor desligado. Troca de direção custa 100 ms. | normal, jam, missing-upper, missing-lower |
| REQ-07 Passagem | Após abrir: feixe ativo → inativo, com amostra fresca > 1024, em até 10000 ms. Só então cria sessão (entrada) ou remove sessão paga (saída). | normal, obstruction-before-close |
| REQ-08 Fechamento | Verde off + vermelho on **antes** de fechar, feixe livre. Feixe ativo durante fechamento → motor off em ≤ 10 ms, sem reverter nem religar → assistência. | obstruction-before-close, obstruction-during-close |
| REQ-09 Assistência | Motor off, verde off, vermelho on, pulso de 100 ms no buzzer. Rearme com cancela fechada: distância > 1024 e feixe livre. Falha de posição parcial, fins de curso contraditórios e falha de ADC: travado até reset. | jam, low-confidence, payment-missing |
| REQ-10 ADC4 | `adc_read_async`, uma conversão por vez alternando canais 17/18, buffers/sequência/`k_poll_signal` estáticos (sobrevivem a conclusão atrasada), verificação não bloqueante (`k_poll_signal_check`), conclusão observada < submissão+100 ms; senão falha travada. Segurança continua enquanto pendente. | noisy-threshold, stationary |
| REQ-11 UART | Dois analisadores independentes e limitados: quadro ≤ 128 bytes com terminador, montagem parcial com prazo de 1000 ms, descarte até LF do que for malformado/longo, nunca executar prefixo. Câmera CRLF; P1 só LF, um espaço entre campos, sem CR. | confidence-retry, payment-stale |
| REQ-12 Display/tarifa | ST7789V via SPI1: reset HW (10 ms baixo, 120 ms), SWRESET (+150 ms), SLPOUT (+120 ms), COLMOD 55, MADCTL 00, DISPON; desenho por CASET/RASET/RAMWR (RGB565, byte alto primeiro). Tela: placa, `BRL r.cc`, `SIMULATED PAYMENT`, `NOT PIX`. Tarifa `200 + (max(1,ceil(t/unit)) − 1)×100`, t desde a passagem de entrada; congelada na 1ª cobrança (depois de desenhar a placa, antes de desenhar valor e enviar CHARGE); > 9999 unidades → assistência. | normal, payment-delayed |
| REQ-13 Reset | Reset limpa sessões, pedidos pendentes e saídas de forma coerente; resposta atrasada de antes do reset não autoriza nada. | reset-capture, reset-payment |

## Decisões confirmadas

- Trabalho **individual** (o enunciado fala em dupla). O `REGISTRO_IA.md` terá um só autor, e o aluno defende todo o código.
- Repositório privado: `https://github.com/JeysonNascimento/estacionamento.git`, branch `solucao`.
- Commits **sem nenhuma referência ao Claude/IA** (o uso de IA fica só no `REGISTRO_IA.md`). Um commit por passo verificado.
- Comentários do código em **português**. Identificadores em inglês.
- Explicação de cada requisito em `REQUISITOS.md`, que vai para `student/docs/` na entrega.

## Premissas (corrijam se estiverem erradas)

1. O laboratório ainda **não está instalado** nesta máquina (não há `~/estacionamento`, nem Renode 1.17.0; há `~/zephyr-sdk-1.0.1`). O primeiro passo é seguir `docs/instalacao.md` **usando o repositório privado** no `west init`, e o código vive em `~/estacionamento/emul-parking-lot/student/`, não nesta pasta.
2. Arquitetura: **um laço principal com tick de 10 ms** (`k_sleep`) servindo as duas faixas, mais callbacks GPIO/UART que só registram eventos e uma `k_work` (fila do sistema) para desligar o motor imediatamente em fim de curso/obstrução. Sem uma thread por faixa (menos concorrência para explicar na sabatina).
3. UART por interrupção (`uart_irq_callback_set` + ring buffer) — o enunciado não exige um modo; se o modelo não suportar bem IRQ, cai para `uart_poll_in` no tick (8 bytes a cada 10 ms cabem).
4. Fonte do display: bitmap 5×7 (ou 8×8) próprio, escalado ×2, desenhado com CASET/RASET/RAMWR por bloco — sem o driver `display` do Zephyr (o contrato não garante compatibilidade).
5. UUID: gerado por PRNG próprio (xorshift) + contador, formatado como v4; unicidade só precisa valer dentro de um reset.
6. IDs de captura: contador por faixa a partir de 1 (1..999999999), recomeça no reset (o reset também limpa a câmera).
7. A spec e o plano ficam também em `student/docs/` para seguirem no PR.

## Módulos e interfaces (contrato a combinar no dia 1)

Lógica pura (testável com ztest+FFF) separada da camada fina de drivers:

| Módulo | Responsabilidade | Depende de |
|---|---|---|
| `hw` (`hw.c/.h`) | Única camada que chama drivers: `hw_motor(lane, en, dir)`, `hw_light`, `hw_buzzer`, leitura de limites/feixe, callbacks EXTI, `hw_uart_tx`, `hw_spi_*` | Zephyr |
| `adc_sampler` | Dono do pedido async: alterna canais, prazo 100 ms, falha travada, expõe `amostra fresca` por faixa | `hw` (adc) |
| `line_parser` | Montador de linhas limitado (128 B, prazo 1000 ms, descarte até LF), instância por UART | — |
| `camera` | Formata `AT+CAPTURE`, interpreta `OK`/`+PLATE`/`ERROR`, valida placa/ID/confiança | `line_parser` |
| `payment` | Formata CHARGE/CANCEL, interpreta RESULT/ERROR, UUID, retentativas | `line_parser` |
| `tariff` | `tariff_compute(elapsed_ms, unit_ms, &centavos)` com limite de 9999 unidades | — |
| `sessions` | Tabela por placa, capacidade, criar/encerrar/consultar, `t_entrada` | — |
| `barrier` | FSM do motor: FECHADA/ABRINDO/ABERTA/FECHANDO/FALHA, prazos 2500 ms, obstrução | `hw` |
| `display` | Init ST7789V, fonte, `display_show_charge(plate, centavos)` | `hw` (spi) |
| `lane` | FSM da faixa: IDLE → IDENTIFICANDO → (PAGANDO) → ABRINDO → PASSAGEM → FECHANDO → IDLE / ASSISTÊNCIA | todos acima |
| `main` | Init, laço de 10 ms, chama `lane_step()` das duas faixas | `lane` |

Eventos que a FSM da faixa recebe: `PLATE_OK(placa)`, `PLATE_REJECT`, `CAPTURE_TIMEOUT`, `PAID`, `PAY_FAIL`,
`LIMIT_UPPER/LOWER`, `BEAM_ON/OFF`, `DIST(código, fresco)`, `ADC_FAULT`, `DEADLINE`.

## Tech Stack

Zephyr v4.5.0 · Zephyr SDK 1.0.1 (arm-zephyr-eabi) · Renode 1.17.0 portátil · Python 3.12 + west ·
ztest + FFF (`zephyr/fff.h`) · gcov (cobertura) · GCC `-fanalyzer` (estática).

## Commands

```sh
. ~/estacionamento/ambiente.sh && cd "$LAB"
# um cenário (headless; unit de tarifa = 1000 ms)
python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
  --output build/<nome-novo> --headless <cenario>
# laboratório no navegador
python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" --output build/lab-NN
# cobertura (todos os cenários públicos + suítes de student/tests)
python scripts/firmware-coverage.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
  --output build/cobertura-NN --jobs 4
# análise estática
python scripts/static-check.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
  --output build/estatica-NN
```
Resultado de cada cenário: `build/<nome>/assessment/results.json`.

## Project Structure (só `student/` é entregue)

```
student/
├── CMakeLists.txt, Kconfig, prj.conf, boards/   (base fornecida; prj.conf + UART por IRQ)
├── src/        config.h (constantes), hw.c (única camada que chama drivers),
│               barrier.c, lane.c, adc_sampler.c, line_parser.c, camera.c,
│               payment.c, tariff.c, sessions.c, display.c, font.c, main.c
├── tests/      adc/, barrier/, display/, hw/ (drivers reais), lane/, proto/,
│               common/hw_fake.c (falsos FFF de hw.h)
├── docs/       SPEC.md, REQUISITOS.md
└── REGISTRO_IA.md
```

## Code Style

C11 no estilo Zephyr (tabs, 100 colunas), retorno `int` com `-errno`, sem alocação dinâmica,
buffers estáticos com tamanho explícito, toda função de driver checa `rc`. Nomes em inglês,
comentários curtos (pt ou en, consistente). Exemplo:

```c
/** Liga o motor no sentido pedido; nunca habilita sem antes fixar a direção. */
int hw_motor_drive(enum lane_id lane, bool open)
{
	const struct lane_hw *h = &lane_hw[lane];
	int rc = gpio_pin_set_dt(&h->direction, open ? 1 : 0);

	if (rc != 0) {
		return rc;
	}
	return gpio_pin_set_dt(&h->enable, 1);
}
```

## Testing Strategy

- **Cenários (Renode)**: verdade final. Um commit por passo verificado, mensagem com o cenário que passou.
- **Unit (ztest+FFF em `student/tests/<suíte>/`)**: parsers (fragmentos, 128 B, prazo, prefixos, ID/UUID/valor errados), tarifa (limites 60000/60001, 9999/10000 unidades), sessões (cheio, duplicada), FSM de cancela/faixa com `hw_*` falsos, e **caminhos de erro de driver** (`return_val = -EIO`, `device_is_ready` falso) que cenário nenhum alcança. Todo teste com `zassert_*`.
- **Cobertura**: `firmware-coverage.py`; `LCOV_EXCL_LINE` só com motivo, ≤ 3%.

## Boundaries

- **Always:** só alterar `student/`; drivers reais (GPIO/EXTI, `adc_read_async`, UART, SPI); prazos em tempo virtual (`k_uptime_get`); rodar o cenário após cada mudança; atualizar `REGISTRO_IA.md` com autor; trabalhar no branch `solucao`.
- **Ask first:** novas opções Kconfig/`prj.conf` além do necessário; exclusões `LCOV_EXCL_LINE`; qualquer ação que torne código público (fork, push para `entrega`).
- **Never:** editar `hardware/ models/ scripts/ tests/ web/` ou docs; fabricar leituras ou escrever no estado dos modelos; imprimir no console o que deve ir ao display; detectar cenário; `git push --force`; compartilhar código com outras duplas.

## Success Criteria

1. `student/` compila numa árvore limpa (G1).
2. Os 19 cenários públicos passam em `--headless`, incluindo os 4 do G3.
3. `firmware-coverage.py`: linhas ≥ 95%, branches ≥ 90%, nenhum cenário/suíte reprovado.
4. `static-check.py`: 0 achados.
5. Teste manual no navegador com as **duas faixas simultâneas** (entrada lotando + saída pagando; falha numa faixa enquanto a outra opera).
6. `REGISTRO_IA.md` com entradas dos dois alunos; descrição do PR com IDs de execução e tempos virtuais.

## Open Questions

Nenhuma. Resolvidas em 2026-11-18: **laço único** de 10 ms + `k_work` para a parada imediata;
**UART por interrupção** (o callback só põe os bytes num ring buffer, e o laço consome);
o repositório privado está **vazio**, então a instalação faz o push inicial da base do curso.
