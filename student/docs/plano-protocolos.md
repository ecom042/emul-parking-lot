# Plano: blocos de protocolo e drivers finos (parte do colega)

Branch: `feat/protocols`, a partir do `solucao` (`e53b6c4`).
Base: [`contrato-e-divisao.md`](contrato-e-divisao.md) (rascunho) e a
[especificação](../../docs/student-specification.md).

## 1. Objetivo e limite

Implementar agora tudo o que a especificação já define e que **não depende**
dos pontos em aberto do contrato (quem chama `poll`, o que `payment_start` faz
por dentro, evento da tarifa > 9999). São blocos que a `camera`, a `distance`,
o `payment` e a `screen` vão só juntar depois.

**Não tocar** nos arquivos de contrato: `services/events.h`, `hw/lane_hw.h`,
`fsm/*`, `main.c`. Os únicos arquivos compartilhados alterados são as listas
dos `CMakeLists.txt` (conflito trivial: manter as duas linhas).

**Fica para depois do contrato:** `services/camera`, `services/payment`,
`services/distance`, `services/screen`, geração de UUID, campos novos de
`lane_hw`.

## 2. Visão dos módulos

| Módulo | Camada | Depende de | Suíte de teste |
|---|---|---|---|
| `services/wire` | lógica pura | — | `tests/unit/wire/` |
| `services/line_framer` | lógica pura | — | `tests/unit/line_framer/` |
| `services/camera_proto` | lógica pura | `wire` | `tests/unit/camera_proto/` |
| `services/p1_proto` | lógica pura | `wire` | `tests/unit/p1_proto/` |
| `services/text` | lógica pura | — | `tests/unit/text/` |
| `hw/uart_line` | driver fino | driver UART do Zephyr | `tests/hw/uart_line/` (`zephyr,uart-emul`) |
| `hw/adc4` | driver fino | `adc_read_async` | `tests/hw/adc4/` (`zephyr,adc-emul`) |
| `hw/st7789` | driver fino | SPI + GPIO | `tests/hw/st7789/` (modelo real no Renode) |

Fluxo futuro (para entender onde cada peça entra):

```text
camera:  uart_line_read -> line_framer_feed -> camera_proto_parse -> (regras de tentativa) -> events_post
payment: p1_proto_format_charge -> uart_line_write ... uart_line_read -> line_framer -> p1_proto_parse
screen:  text_format_brl / text_render_row -> st7789_set_window + st7789_write_pixels
distance: adc4_start / adc4_poll -> (prazo 100 ms, falha travada) -> zbus chan_distance
```

Convenções (as mesmas do código do Kauã): headers com Doxygen em inglês,
retorno `int` com `0`/valor ou `-errno`, nada bloqueia além do `st7789_init`,
identificadores e comentários em inglês, `__ASSERT` só para erro de
programação (ponteiro nulo), nunca para dado vindo do fio.

---

## 3. Passo 0: experimento de UART no Renode (não vai para o commit)

**Pergunta:** ler a câmera com `uart_poll_in()` a cada ~1 ms perde bytes?
O modelo entrega fragmentos de até 8 bytes num mesmo instante; o RDR do STM32
real só guarda 1 byte, mas o `STM32F7_USART` do Renode costuma enfileirar.
A especificação diz que o modelo não promete "general application IRQ
transport", o que favorece polling.

**Como:** um teste temporário em `tests/hw/` que, na placa real no Renode,
envia `AT+CAPTURE=1\r\n` na `entry-camera` (USART3) e lê com `uart_poll_in`
por até 1000 ms, imprimindo os bytes. Esperado:
`OK\r\n+PLATE:1,"ABC1D23",93\r\n` inteiro. Repetir com `k_msleep(1)` entre
leituras (pior caso).

**Decisão:**
- chegou inteiro → `uart_line` com polling (plano abaixo);
- perdeu bytes → `uart_line` com `CONFIG_UART_INTERRUPT_DRIVEN` e `ring_buffer`
  de 256 B por UART (mesma API pública; só muda o `.c`).

Também verificar, no mesmo experimento, se a suíte `tests/hw` roda com os
modelos de câmera/terminal/display ligados (útil para o passo 8).

Registrar o resultado no `REGISTRO_IA.md` e apagar o teste temporário.

---

## 4. Passo 1: `services/wire` (validadores do fio)

Regras de sintaxe usadas pelos dois protocolos. Um só lugar, testado uma vez.

```c
/** Canonical decimal: 1+ digits, no sign, no leading zero (except "0"). */
int wire_parse_uint(const char *s, size_t len, uint32_t max, uint32_t *out);
/** [A-Z]{3}[0-9][A-Z0-9][0-9]{2}, exactly 7 chars. */
bool wire_is_plate(const char *s, size_t len);
/** Lowercase hex 8-4-4-4-12, exactly 36 chars. */
bool wire_is_uuid(const char *s, size_t len);
```

`wire_parse_uint`: `-EINVAL` para vazio, não-dígito, zero à esquerda, `+`/`-`;
`-ERANGE` acima de `max` (sem overflow: comparar antes de multiplicar).

**Testes (`tests/unit/wire/`):**
- `"0"`, `"1"`, `"999999999"` com max 999999999 → ok; `"1000000000"` → `-ERANGE`;
  `"4294967296"` (estoura 32 bits) → `-ERANGE`.
- `""`, `"01"`, `"+1"`, `"-1"`, `"1a"`, `" 1"` → `-EINVAL`.
- Placas: `ABC1D23`, `ABC1234` válidas; `abc1d23`, `ABC-1D23`, `AB1CD23`,
  `ABC1D2`, `ABC1D234`, `ABC1D2A`, `ABÇ1D23` inválidas.
- UUID: válido; maiúscula; hífen fora do lugar; 35 e 37 chars; `g`.

**Commit:** `feat(wire): validate canonical numbers, plates and UUIDs`

---

## 5. Passo 2: `services/line_framer` (montagem de linhas)

Uma instância por UART (câmera de entrada, câmera de saída, terminal).
Sem alocação, sem threads; quem chama dá os bytes e o tempo.

```c
#define LINE_MAX 128 /* bytes including the terminator */

enum line_mode { LINE_CRLF, LINE_LF };          /* camera / payment */
enum line_result { LINE_NONE, LINE_READY, LINE_DROPPED };

struct line_framer {
	enum line_mode mode;
	char buf[LINE_MAX];   /* NUL-terminated line, terminator stripped */
	size_t len;
	bool discarding;      /* malformed/overlong/expired: skip through LF */
	bool started;
	int64_t started_ms;
};

void line_framer_init(struct line_framer *f, enum line_mode mode);
enum line_result line_framer_feed(struct line_framer *f, uint8_t byte, int64_t now_ms);
/** Expire a partial line older than 1000 ms (call even without new bytes). */
enum line_result line_framer_check(struct line_framer *f, int64_t now_ms);
const char *line_framer_line(const struct line_framer *f); /* valid after LINE_READY */
```

**Regras (todas da especificação):**
- corpo só ASCII imprimível `0x20..0x7E`; qualquer outro byte → descartar até LF;
- `LINE_CRLF`: termina em `\r\n`; `\r` sem `\n` em seguida ou `\n` sem `\r` →
  linha descartada; corpo máximo 126;
- `LINE_LF`: termina em `\n`; qualquer `\r` → descartar; corpo máximo 127;
- passou de 128 com o terminador → descartar até LF (nunca entregar o prefixo);
- linha vazia → `LINE_DROPPED`;
- parcial com mais de 1000 ms desde o primeiro byte → descartar o resto até LF;
- `LINE_DROPPED` é devolvido uma vez, no LF que encerra o descarte (útil para
  contar erros e para testar).

**Testes (`tests/unit/line_framer/`):**
- linha CRLF inteira; a mesma em fragmentos de 1 e de 8 bytes;
- duas linhas seguidas no mesmo lote;
- LF puro em modo CRLF; CR em modo LF; CR no meio;
- 126 e 127 bytes de corpo em CRLF (aceita / descarta); 127 e 128 em LF;
- linha longa seguida de linha boa: a boa sai intacta;
- byte `0x00` e `0x80` no corpo;
- prazo: bytes em t=0 e t=1001 → descarta; `line_framer_check` em t=1001 sem
  bytes → descarta; exatamente 1000 ms → aceita (fronteira, documentar);
- depois de um descarte, a próxima linha funciona.

**Commit:** `feat(line_framer): assemble bounded CRLF/LF lines with a deadline`

---

## 6. Passo 3: `services/camera_proto`

Formata comandos e interpreta uma linha já montada (sem `\r\n`). Não sabe nada
de tentativas nem de prazos: isso é da futura `camera`.

```c
#define PLATE_LEN 7
#define CAPTURE_ID_MAX 999999999u

enum camera_reply_kind { CAMERA_REPLY_OK, CAMERA_REPLY_ERROR,
			 CAMERA_REPLY_PLATE, CAMERA_REPLY_INVALID };

struct camera_reply {
	enum camera_reply_kind kind;
	uint32_t capture_id;          /* PLATE only */
	char plate[PLATE_LEN + 1];    /* PLATE only */
	uint8_t confidence;           /* PLATE only, 0..100 */
};

/** "AT+CAPTURE=<id>\r\n". @return length, -EINVAL bad id, -ENOSPC small buffer. */
int camera_proto_format_capture(uint32_t id, char *out, size_t size);
void camera_proto_parse(const char *line, struct camera_reply *reply);
```

Formato aceito, exato: `+PLATE:<id>,"<placa>",<conf>`. A regra "≥ 85" e "ID da
tentativa atual" **não** fica aqui (é regra da `camera`); aqui só a sintaxe.

**Testes:** `OK`, `ERROR`, PLATE padrão (`+PLATE:1,"ABC1D23",93`);
id `0`, `01`, `1000000000`; conf `101`, `085`, vazia; placa minúscula, sem
aspas, com espaço; vírgula a mais; texto depois; `ok`; `OK ` com espaço.
Formatação: id 1, id máximo, id 0 (`-EINVAL`), buffer curto (`-ENOSPC`).

**Commit:** `feat(camera_proto): format captures and parse camera replies`

---

## 7. Passo 4: `services/p1_proto`

```c
#define UUID_STR_LEN 36
#define AMOUNT_MAX 1000000u

struct p1_uuid { uint8_t bytes[16]; };

enum p1_reply_kind { P1_PAID, P1_CANCELLED, P1_ERROR, P1_INVALID };
enum p1_error { P1_ERR_FRAME, P1_ERR_TIMEOUT, P1_ERR_CAPACITY,
		P1_ERR_CONFLICT, P1_ERR_UNKNOWN };

struct p1_reply {
	enum p1_reply_kind kind;
	char uuid[UUID_STR_LEN + 1];  /* PAID/CANCELLED */
	uint32_t amount;              /* PAID/CANCELLED */
	enum p1_error error;          /* ERROR */
};

void p1_uuid_to_str(const struct p1_uuid *uuid, char out[UUID_STR_LEN + 1]);
/** "P1 CHARGE <uuid> <plate> <amount> BRL\n". -EINVAL on bad field, -ENOSPC. */
int p1_proto_format_charge(const char *uuid, const char *plate, uint32_t amount,
			   char *out, size_t size);
/** "P1 CANCEL <uuid>\n". */
int p1_proto_format_cancel(const char *uuid, char *out, size_t size);
void p1_proto_parse(const char *line, struct p1_reply *reply);
```

Separação por **um** espaço exato (espaço duplo, inicial ou final → inválido);
número exato de campos; `BRL` sensível a maiúsculas. Comparar UUID e valor com
a cobrança atual é regra do futuro `payment`, não daqui.

**Testes:** `RESULT <uuid> PAID 200 BRL`; `CANCELLED`; os 5 `ERROR`;
`ERROR XYZ`; `paid`; `brl`; `200.00`; `0200`; `0`; `1000001`; UUID maiúsculo;
espaço duplo; campo a mais/a menos; `P2 ...`. Formatação: CHARGE e CANCEL
exatos (comparar a string inteira), valor 0 e 1000001 → `-EINVAL`, placa
inválida → `-EINVAL`, buffer curto. `p1_uuid_to_str` com bytes conhecidos
(`00..ff`) → string esperada e `wire_is_uuid` verdadeiro.

**Commit:** `feat(p1_proto): format charges and parse P1 replies`

---

## 8. Passo 5: `services/text` (fonte e textos da tela)

Fonte própria 5×7 (coluna de 1 px de espaço → célula 6×8), só com os
caracteres necessários: `A–Z`, `0–9`, `.`, espaço. Escala inteira (proposta: 2,
célula 12×16 px; "SIMULATED PAYMENT" = 17 × 12 = 204 px < 240).

Desenho **por linha de pixels**, para não precisar de framebuffer de
240×320×2 = 150 KB: o `st7789` abre a janela do texto e manda linha por linha.

```c
#define TEXT_CELL_W 6
#define TEXT_CELL_H 8

/** "BRL <reais>.<cc>", e.g. 1230 -> "BRL 12.30". @return length or -ENOSPC. */
int text_format_brl(uint32_t cents, char *out, size_t size);
/** Width in pixels of @p s at @p scale. -EINVAL if a char has no glyph. */
int text_width(const char *s, unsigned int scale);
/**
 * One pixel row (0 .. TEXT_CELL_H*scale-1) of @p s, as RGB565 high byte first.
 * @p out must hold text_width()*2 bytes.
 */
int text_render_row(const char *s, unsigned int scale, unsigned int row,
		    uint16_t fg, uint16_t bg, uint8_t *out, size_t size);
```

**Testes:** `text_format_brl` com 0, 5, 200, 1230, 100000, 1000000;
`text_width("A", 1) == 6`, `("AB", 2) == 24`, caractere sem glifo → `-EINVAL`;
`text_render_row` de `"I"` linha 0 escala 1 confere bytes (fg/bg, ordem alta
primeiro); escala 2 duplica pixels e linhas; `row` fora do intervalo e buffer
curto → `-EINVAL`/`-ENOSPC`.

**Commit:** `feat(text): add a 5x7 font and the BRL amount format`

---

## 9. Passo 6: `hw/uart_line`

Camada fina sobre o driver UART. Recebe o `const struct device *`; quem sabe o
alias é o `lane_hw` (depois do contrato) ou o próprio serviço.

```c
int uart_line_init(const struct device *dev);              /* -ENODEV if not ready */
/** Drain received bytes without blocking. @return count (0 = nothing). */
int uart_line_read(const struct device *dev, uint8_t *buf, size_t size);
int uart_line_write(const struct device *dev, const char *data, size_t len);
```

Versão polling: `uart_poll_in` em laço até `-1`; `uart_poll_out` por byte.
(Se o passo 0 decidir por interrupção: `uart_irq_callback_user_data_set`,
`ring_buf` estático por dispositivo, `uart_line_read` copia do anel.)

**Teste (`tests/hw/uart_line/`)** com `zephyr,uart-emul` no `app.overlay`
(`CONFIG_EMUL=y` no `prj.conf` da suíte):
- `uart_emul_put_rx_data("OK\r\n")` → `uart_line_read` devolve 4 bytes iguais;
  segunda leitura → 0;
- buffer menor que os dados → lê `size`, o resto fica para a próxima;
- `uart_line_write` → `uart_emul_get_tx_data` igual ao enviado;
- `uart_line_init` com dispositivo pronto → 0. (O lado `-ENODEV` não é
  alcançável com dispositivos reais: candidato a `LCOV_EXCL_LINE` com motivo,
  ou um nó `status = "disabled"`; decidir no passo.)

**Commit:** `feat(hw): add uart_line, a non-blocking UART byte reader`

---

## 10. Passo 7: `hw/adc4`

O dono único do ADC4 (a especificação pede: a trava do contexto ADC vale para
a leitura inteira, então nunca duas leituras ao mesmo tempo).

```c
int adc4_init(const struct device *adc, const struct adc_channel_cfg *cfgs, size_t count);
/** Start one conversion. -EBUSY while the previous one is still owned. */
int adc4_start(uint8_t channel);
/**
 * Check without blocking. -EAGAIN still pending; 0 done, *code = raw sample
 * (ownership released); <0 the driver's error (ownership released).
 */
int adc4_poll(uint16_t *code);
bool adc4_busy(void);
```

- `buffer`, `adc_sequence`, `k_poll_signal` **estáticos** (sobrevivem a uma
  conclusão atrasada, como pede a especificação);
- "ownership" só é liberada quando o sinal chega; desistir por prazo (100 ms)
  é decisão da futura `distance`, que trava a falha; o `adc4` nunca ressubmete
  enquanto o pedido é dono do buffer;
- amostra > 4095 → `-EIO` (amostra inválida também é falha).

No firmware: `DEVICE_DT_GET(DT_PARENT(DT_ALIAS(entry_distance)))` e
`ADC_CHANNEL_CFG_DT(DT_ALIAS(entry_distance))`/`exit_distance`.

**Teste (`tests/hw/adc4/`)** com `zephyr,adc-emul` (`nchannels` ≥ 19,
`CONFIG_ADC_EMUL=y`, `CONFIG_ADC_ASYNC=y`):
- `adc_emul_const_raw_value_set(17, 683)` → start, poll até 0, code 683;
- start duas vezes → segunda `-EBUSY`; depois do poll → start volta a funcionar;
- poll sem start → `-EINVAL` (ou o código escolhido);
- canal 18 com valor diferente;
- canal não configurado → erro do driver propagado.

**Commit:** `feat(hw): add adc4, one asynchronous conversion at a time`

---

## 11. Passo 8: `hw/st7789`

```c
struct st7789 {
	struct spi_dt_spec spi;
	struct gpio_dt_spec dc;     /* low = command, high = data */
	struct gpio_dt_spec reset;  /* active low */
};
#define ST7789_W 240
#define ST7789_H 320

/** Hardware reset, SWRESET, SLPOUT, COLMOD 55, MADCTL 00, DISPON. ~400 ms, blocks. */
int st7789_init(const struct st7789 *lcd);
/** CASET/RASET + RAMWR. -EINVAL if x0>x1, y0>y1 or outside 240x320. */
int st7789_set_window(const struct st7789 *lcd, uint16_t x0, uint16_t y0,
		      uint16_t x1, uint16_t y1);
/** Pixel bytes after set_window (RGB565, high byte first). */
int st7789_write_pixels(const struct st7789 *lcd, const uint8_t *data, size_t len);
int st7789_fill(const struct st7789 *lcd, uint16_t x0, uint16_t y0,
		uint16_t x1, uint16_t y1, uint16_t color);
```

Sequência (esperas com `k_msleep`, tempo virtual): reset ativo 10 ms, solta,
120 ms; `01` + 150 ms; `11` + 120 ms; `3A 55`; `36 00`; `29`.
Devicetree: `SPI_DT_SPEC_GET(DT_ALIAS(payment_display), SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0)`,
`GPIO_DT_SPEC_GET(..., dc_gpios)`, `reset_gpios`.

**Teste (`tests/hw/st7789/`):** o Zephyr não tem emulador SPI que capture
bytes sem escrever um alvo emulado. Proposta:
- `st7789_set_window` com janelas inválidas → `-EINVAL` sem tocar no SPI;
- se o passo 0 confirmar que a suíte roda com o modelo ST7789 ligado:
  `st7789_init`, uma janela e um `fill` devolvem 0 na placa real;
- a prova de que os pixels estão certos vem do cenário `normal` (M3). Antes
  disso, conferir manualmente no monitor do Renode (`display GetPixel x y`).

**Commit:** `feat(hw): add the st7789 display driver over SPI1`

---

## 12. Passo 9: qualidade e registro

1. `scripts/static-check.py` → zero achados (rodar também ao fim de cada passo).
2. `scripts/firmware-coverage.py` → anotar linhas/branches; olhar os branches
   descobertos dos módulos novos e cobrir com teste (não com exclusão).
3. Cada `LCOV_EXCL_LINE` com o motivo no próprio comentário e anotado aqui
   (vai cair na sabatina).
4. Entrada no `student/REGISTRO_IA.md` (Placido): pedido, proposta, o que foi
   aceito/rejeitado, resultados das medições.
5. Atualizar `docs/contexts/estado-atual.md` e criar
   `docs/contexts/sessoes/<data>-placido.md`.
6. PR interno `feat/protocols` → `solucao`, revisado pelo Kauã.

---

## 13. Verificação de cada passo

```sh
. ../ambiente.sh
west build -b parking_lot_u5 student/tests/unit -d build/pl-unit-<passo>   # passos 1-5
west build -b parking_lot_u5 student/tests/hw   -d build/pl-hw-<passo>     # passos 6-8
west build -b parking_lot_u5 student            -d build/pl-fw-<passo>     # firmware compila
python scripts/firmware-coverage.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/pl-cov-<passo> --jobs 4
python scripts/static-check.py   # ver docs/grading-tools.md para os argumentos
```

Critério de pronto de um passo: suíte passa no `firmware-coverage.py`, firmware
compila, `static-check` sem achados novos, pasta `build/` antiga apagada,
commit feito pelo aluno. Os cenários do Renode ainda não passam com estes
blocos sozinhos (é esperado: eles entram pela `camera`/`payment`/`distance`
nos marcos M2/M3).

## 14. Riscos

| Risco | Efeito | Mitigação |
|---|---|---|
| Polling perde bytes no Renode | câmera/pagamento falham por timeout | passo 0 decide antes de escrever `uart_line` |
| `uart-emul`/`adc-emul` exigem configs que conflitam com a placa | suíte `tests/hw` não compila | se conflitar, suíte nova `tests/hw_io/` só para eles |
| Display sem teste automático de pixels | erro de desenho só aparece no M3 | conferir no monitor do Renode ao fim do passo 8 |
| Contrato muda nomes (ex.: `PLATE_LEN` em `camera.h`) | renomear | constantes ficam em `camera_proto.h`; `camera.h` reaproveita |
| Conflitos nos `CMakeLists.txt` com o Kauã | merge manual | manter as duas linhas; `git merge solucao` com frequência |
