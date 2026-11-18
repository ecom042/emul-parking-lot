# Registro de uso de IA

## Entrada 1
- **Autor**: Aluno
- **Ferramenta**: Claude (Antigravity)
- **Modelo**: Claude Opus 4.6
- **O que foi pedido**: Implementação completa do firmware do estacionamento com duas faixas, seguindo os requisitos REQ-01 a REQ-13.
- **O que foi proposto**: Arquitetura modular com separação em camadas:
  - `hw.c/h` — Camada fina de hardware (GPIO, UART, ADC, SPI)
  - `lane.c/h` — Máquina de estados da faixa (reutilizada para entrada e saída)
  - `camera.c/h` — Protocolo AT da câmera (parser UART com CRLF)
  - `payment.c/h` — Protocolo P1 do terminal (parser UART com LF)
  - `session.c/h` — Tabela de sessões (até CONFIG_PARKING_CAPACITY)
  - `display.c/h` — Driver ST7789V via SPI
  - `adc.c/h` — Leitura assíncrona do ADC4
  - `font.c/h` — Fonte bitmap 8x16 para o display
- **O que foi aceito**: Código completo gerado e integrado.
- **Verificação**: Aguardando execução dos cenários no ambiente de simulação.
