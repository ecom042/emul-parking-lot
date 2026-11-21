# Retomar o trabalho do Plácido (etapas 2 a 5)

Prompt para colar numa sessão nova do Claude Code, na raiz do repositório.

```text
Você é o Claude Code ajudando o Plácido (o "colega" da dupla) no firmware do
estacionamento. Responda em português. Leia, nesta ordem: AGENTS.md,
student/docs/contexts/estado-atual.md, student/docs/contexts/sessoes/2026-11-21-placido.md,
student/docs/contrato-e-divisao.md e student/docs/para-kaua.md. Depois
confira `git branch --show-current` e `git status`.

O que já está pronto: wire, line_framer, camera_proto, p1_proto (com
p1_uuid_format), text, hw/uart_line, hw/adc4, hw/st7789 e a etapa 1 (M0):
events.h completo, campos camera/distance_cfg em lane_hw, payment_terminal_get
e os headers camera.h, distance.h, payment.h e screen.h (só declarações).

Falta, nesta ordem, uma etapa por branch (feat/camera, feat/distance,
feat/screen, feat/payment), cada uma a partir da anterior:
 2. services/camera   (camera_init/start/abandon/poll/plate; 3 tentativas com ID
    novo, 1000 ms cada, confiança >= 85, EV_PLATE_READ / EV_CAPTURE_FAILED;
    usa uart_line, line_framer, camera_proto)
 3. services/distance (adc4 alternando canais 17/18, uma conversão por vez,
    conclusão em < 100 ms, chan_distance no zbus, falha trava e posta
    EV_ADC_FAULT nas duas faixas)
 4. services/screen   (screen_init, screen_show_plate, screen_show_amount sobre
    hw/st7789 e services/text)
 5. services/payment  (payment_init, payment_start(plate, cents), payment_cancel,
    payment_poll; UUID por contador com p1_uuid_format guardado para reenvios;
    3 envios de 1000 ms; só PAID exato; usa uart_line, line_framer, p1_proto)

Processo de CADA etapa:
 a. Crie o branch da etapa a partir do anterior.
 b. Suba UM subagente Opus (só leitura) para devolver um plano enxuto
    (arquivos, assinaturas, testes, riscos, verificação). Depois implemente o
    plano você mesmo, ou com subagente Sonnet se for pequeno.
 c. Testes: ztest + FFF em student/tests/unit/<modulo>/ (fakes de hw/ e de
    events_post quando precisar) e registre no CMakeLists de tests/unit.
 d. Verifique UMA vez: python scripts/firmware-coverage.py e
    python scripts/static-check.py (comandos em student/docs/contexts/sessoes/
    2026-11-20-kaua.md; `. ../ambiente.sh` antes). Todas as suítes devem passar,
    static-check com 0 achados, cobertura sem cair. Os cenários reprovando é
    esperado até o main.c do Kauã chamar os serviços.
 e. Máximo 2 correções por falha; se continuar falhando, pare e me explique.
    Não entre em ciclo de testar e errar. Apague as pastas build/ que criar.
 f. Acrescente uma entrada em student/REGISTRO_IA.md e atualize estado-atual.md.
 g. Commit (só arquivos de student/, com o trailer Co-Authored-By do
    ambiente) e push do branch. Nunca push --force, nunca mexa no main nem no
    solucao. Só suba depois de a verificação passar.

Não altere hardware/, models/, scripts/, tests/ da raiz, web/ nem docs/ da
raiz. Não implemente presence, sessions, tariff, fsm nem main.c: são do Kauã.
Comece pela etapa 2.
```

Pendências conhecidas: polling da UART assumido sem perda de bytes (confirmar
quando a câmera estiver ligada); `st7789` sem teste automático de pixels.
