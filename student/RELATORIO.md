# Relatório da dupla

Escrevam durante a semana, não no fim. Rubrica em
[desafio](../docs/desafio.md#pontos) ("Relatório e projeto"). Use figuras e
tabelas à vontade; o texto vale mais que o volume.

## Dupla e divisão do trabalho

Quem fez cada parte, e como revisaram a parte do colega.

## Máquina de estados

Estados de cada faixa, transições, eventos e prazos (um diagrama ajuda).

## Pinos e interfaces

Como o firmware usa GPIO/EXTI, ADC4, as UARTs e a SPI, e como o código separa
a lógica da camada que chama os drivers.

## ADC4: posse e liveness

Quem é dono da conversão, como os dois canais se alternam e o que acontece
com uma conclusão atrasada.

## Analisadores de UART e novas tentativas

Câmera e terminal: montagem dos quadros, descarte, prazos, IDs e UUIDs,
reenvios.

## Tarifa

Exemplos calculados e o momento em que o valor é congelado.

## Falhas e recuperação

Cada caminho para a assistência e como ele é rearmado.

## Testes e métricas

Suítes de teste de unidade, cobertura final, achados da análise estática e
exclusões `LCOV_EXCL_LINE` com o motivo.

## Limitações conhecidas
