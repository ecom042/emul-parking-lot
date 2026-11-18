# Proposta de interfaces — issue #1

Revisar em dupla antes de fixar assinaturas ou implementar o firmware.

| Responsável | Módulo | Contrato proposto |
| --- | --- | --- |
| Maria Antonia | Controle e sessões | Mantém um estado por faixa e uma tabela compartilhada. Decide quando capturar, cobrar, abrir e fechar. Serializa as mudanças de sessão. |
| Maria Antonia | GPIO e segurança | Recebe comandos do motor e dos sinais. Entrega eventos dos fins de curso e das bordas do feixe. EXTI agenda o corte do motor em trabalho de segurança independente. |
| Maria Antonia | ADC4 | Entrega código de 12 bits e instante de conclusão por faixa ou uma falha global. |
| Jackson Bruno | Câmeras | Recebe pedido de captura ou abandono por faixa. Entrega placa aceita, esgotamento de tentativas ou erro. |
| Jackson Bruno | Tarifa e terminal | Recebe o tempo da sessão para calcular a tarifa. Recebe placa e valor congelado para iniciar a cobrança. Entrega pagamento confirmado, cancelamento ou falha. No abandono, invalida o UUID antes de enviar CANCEL. |
| Jackson Bruno | Display | Recebe pedidos separados para desenhar a placa e a cobrança. Entrega confirmação de desenho ou falha, mantendo o caminho de segurança livre durante SPI. |

UART e SPI podem levar tempo. O controle continua atendendo as duas faixas enquanto espera seus eventos. Eventos de reconhecimento, pagamento e display identificam o pedido que os originou. O controle descarta eventos de pedidos abandonados e usa apenas amostras recentes do ADC nas decisões que dependem de distância.

## Estados, transições e prazos

| Requisito | Comportamento |
| --- | --- |
| REQ-01 | Iniciar com motor e verde desligados e vermelho aceso. Em repouso sem fim de curso inferior, pedir assistência. Fins de curso contraditórios travam até reset. |
| REQ-02 | Com cancela fechada e ADC recente <683, reconhecer. Permitir três tentativas com IDs novos, cada uma com 1000 ms para a resposta. Aceitar placa válida com confiança ≥85 e ID atual. |
| REQ-03 | Durante reconhecimento ou pagamento, ADC recente >1024 abandona a tentativa. Preservar o estado de presença entre os limiares. Um veículo parado após falha aguarda rearme. |
| REQ-04 | Entrada autoriza placa sem sessão e com vaga. Saída só cobra placa com sessão. Espera ou falha local preserva o serviço da outra faixa. |
| REQ-05 | Saída aguarda PAID atual com UUID e valor exatos. Cada espera dura até 1000 ms. Fazer até três envios da mesma cobrança. Cancelamento válido ou esgotamento pede assistência. |
| REQ-06 | Abrir ou fechar com prazo de 2500 ms. Usar o fim de curso para cortar o motor. Acender verde após confirmar o fim superior com motor desligado. |
| REQ-07 | Após abrir, observar feixe ativo e depois livre, com ADC recente >1024, em até 10000 ms. Nessa transição, criar sessão na entrada ou remover sessão na saída. |
| REQ-08 | Apagar verde, acender vermelho e verificar feixe livre antes de fechar. Se obstruir durante o fechamento, cortar o motor em até 10 ms e entrar em assistência. |
| REQ-09 | Em assistência, desligar motor e verde, acender vermelho e pulsar o buzzer por 100 ms. Rearmar falha recuperável com cancela fechada, feixe livre e ADC recente >1024. Falhas de posição, contradição e ADC ficam travadas até reset. |
| REQ-10 | Alternar os canais com uma conversão assíncrona em voo. Deixar buffer, sequência e sinal persistentes. Verificar a conclusão sem bloquear e antes de 100 ms. Uma conclusão tardia não recupera a aquisição. |
| REQ-11 | Montar quadros independentes de até 128 bytes incluindo terminador. Usar CRLF nas câmeras, LF no terminal e prazo de 1000 ms por quadro parcial. Descartar quadros inválidos até LF. |
| REQ-12 | Inicializar o ST7789V por SPI. Desenhar placa, congelar tarifa, desenhar valor e avisos e enviar CHARGE. A tarifa é 200 centavos pela primeira unidade e 100 pelas seguintes, arredondadas para cima. Acima de 9999 unidades, pedir assistência. |
| REQ-13 | Reset coerente reinicia sessões, pedidos, parsers e estados. Uma resposta só vale para o pedido ativo da operação atual. |

Usar o uptime virtual do Zephyr em todos os prazos. Para ztest/FFF, substituir as operações de periféricos e verificar chamadas e transições com instantes explícitos.

## Cenário inicial

O firmware original compilou no WSL com Zephyr 4.5.0, SDK 1.0.1 e tarifa de 1000 ms. O build `issue-01-west-01` gerou `zephyr/zephyr.elf` para `parking_lot_u5`.

A execução oficial `build/issue-01-normal-01` saiu com código 1 e `Wrong runtime identity` antes da simulação. O Renode oficial Linux x86_64 corresponde ao SHA-256 do guia e informa `1.17.0+20260907gitf1dd1b4af`. O script exige `1.17.0+20260906gitf1dd1b4af`. A tentativa não gerou resultado de cenário nem tempo virtual medido.
