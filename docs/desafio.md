# Desafio: controlador de estacionamento com duas faixas

Este é o enunciado do desafio e a regra de avaliação. O contrato técnico
completo (pinos, protocolos, tempos, modelos) está nos documentos em inglês
ligados abaixo. Quando este texto e um contrato técnico parecerem divergir, o
contrato técnico vale para o comportamento e este texto vale para a nota.

## O desafio

Você vai escrever, em Zephyr RTOS, o firmware de um estacionamento para a placa
`nucleo_u5a5zj_q`. Há uma faixa de entrada e uma de saída funcionando ao mesmo
tempo e até `CONFIG_PARKING_CAPACITY` carros estacionados (padrão 4).

O firmware comanda cancelas, sinais e buzzer por GPIO, mede a distância do
carro pelo ADC4, conversa com duas câmeras e um terminal de pagamento por UART
e desenha o display do terminal por SPI. Nada disso existe fisicamente: o
Renode simula a placa e os equipamentos, e os cenários verificam o que os
periféricos fizeram de fato. **Mensagens no console não contam.** Uma cancela
só "abriu" se o modelo viu o motor ligado na direção certa e o fim de curso
superior acionado.

O ponto de partida em `student/` é inerte de propósito: motor, verde e buzzer
desligados, vermelho aceso. Ele reprova nos cenários até você implementar o
controlador.

Leia primeiro o [setup](setup.md) e a
[especificação do aluno](student-specification.md). Depois consulte os
contratos conforme precisar: [protocolos](device-protocols.md),
[modelos de hardware](hardware-models.md), [modelo físico](physical-model.md)
e [cenários](scenarios.md).

## Regras do jogo

1. **Você só entrega `student/`.** Na avaliação, a sua pasta `student/` é
   copiada para uma árvore limpa do curso. Mudanças em `hardware/`, `models/`,
   `scripts/`, `tests/`, `web/` ou nos documentos são ignoradas. Não adianta
   "consertar" um cenário ou um modelo.
2. **Use os drivers reais do Zephyr**: GPIO/EXTI, ADC4 assíncrono, UARTs e
   SPI. É proibido fabricar leituras, escrever no estado dos modelos pelo host
   ou imprimir o que deveria ser desenhado.
3. **O firmware decide.** Autorização, saídas, novas tentativas, preço e
   sessões são do firmware. Os modelos cuidam de movimento, sensores e
   respostas; os cenários, de estímulos e expectativas.
4. **O tempo é virtual.** Todos os prazos usam o tempo virtual do Renode,
   nunca o relógio do computador.

## Requisitos

Cada requisito resume uma parte da
[especificação](student-specification.md#controller-and-safety-requirements).
Os cenários públicos listados exercitam o requisito. Os cenários ocultos usados
na avaliação exercitam os mesmos requisitos com outras combinações (por
exemplo, as duas faixas ao mesmo tempo).

| ID | Requisito | Cenários públicos |
|---|---|---|
| REQ-01 | **Partida segura.** Começa com motor desligado, vermelho aceso e verde apagado. Sem fim de curso inferior em repouso, pede assistência em vez de fechar sozinho. Fins de curso superior e inferior ao mesmo tempo travam em assistência até o reset. | `partial-startup`, `missing-lower`, `contradictory-limits` |
| REQ-02 | **Reconhecimento da placa.** Com a cancela fechada e código ADC recente < 683 (cerca de 1000 mm), inicia a captura. Aceita placa válida com confiança ≥ 85 para o ID de captura atual. São até três tentativas, cada uma com ID novo e prazo de 1000 ms; esgotadas, pede assistência. | `normal`, `confidence-retry`, `low-confidence`, `camera-timeout` |
| REQ-03 | **Presença, não passagem.** Um carro parado continua presente: nada de capturas repetidas. Ruído do ADC perto do limiar não gera chegadas falsas. Durante identificação ou pagamento, código > 1024 (cerca de 1500 mm) abandona a tentativa. | `stationary`, `noisy-threshold` |
| REQ-04 | **Sessões e faixas simultâneas.** Entra uma placa aceita sem sessão quando há vaga; lotado ou placa já estacionada pede assistência. Na saída, só uma placa com sessão ativa paga e sai. As duas faixas funcionam ao mesmo tempo, e a falha ou espera de uma não atrasa a segurança da outra. | `normal` |
| REQ-05 | **Pagamento.** A cobrança é um valor inteiro imutável, e a cancela só abre com `PAID` exato e atual. Retransmite no máximo três vezes, com o mesmo UUID e o mesmo valor. Respostas ausentes, antigas, malformadas, canceladas ou divergentes nunca abrem a cancela. | `payment-delayed`, `payment-missing`, `payment-duplicate`, `payment-stale` |
| REQ-06 | **Movimento da cancela.** Para pelo fim de curso real, nunca por batente. Abrir e fechar têm 2500 ms cada. O verde só acende depois de chegar ao fim de curso superior **com o motor desligado**. | `normal`, `jam`, `missing-upper`, `missing-lower` |
| REQ-07 | **Passagem.** Depois de abrir, exige feixe ativo, depois inativo, com distância > 1024. Só uma passagem completa cria a sessão na entrada ou a encerra na saída. O prazo de passagem é 10000 ms. | `normal`, `obstruction-before-close` |
| REQ-08 | **Fechamento e obstrução.** Apaga o verde e acende o vermelho antes de fechar, com o feixe livre. Obstrução durante o fechamento desliga o motor em até 10 ms, sem reverter nem religar sozinho. | `obstruction-before-close`, `obstruction-during-close` |
| REQ-09 | **Assistência.** Para o motor, apaga o verde, acende o vermelho e dá um pulso de 100 ms no buzzer. Rearma conforme a [especificação](student-specification.md#controller-and-safety-requirements); falhas de posição, fins de curso contraditórios e falhas do ADC ficam travadas até o reset. | `jam`, `low-confidence`, `payment-missing` |
| REQ-10 | **ADC4 assíncrono.** Uma conversão por vez, com buffers que sobrevivem a uma conclusão atrasada, verificada sem bloquear e concluída antes de 100 ms. O trabalho de segurança continua enquanto a conversão está pendente. | `noisy-threshold`, `stationary` |
| REQ-11 | **UARTs robustas.** Dois analisadores independentes e limitados: quadro máximo de 128 bytes, montagem parcial com prazo de 1000 ms, descarte do que estiver malformado até o LF, sem executar prefixos. | `confidence-retry`, `payment-stale` |
| REQ-12 | **Display e tarifa.** O display ST7789V é desenhado via SPI real, com a placa, `BRL r.cc`, `SIMULATED PAYMENT` e `NOT PIX`. A tarifa é `200 + (unidades − 1) × 100` centavos, congelada na primeira cobrança; acima de 9999 unidades, assistência. | `normal`, `payment-delayed` |
| REQ-13 | **Reset.** Um reset limpa sessões, pedidos pendentes e saídas de forma coerente; uma resposta atrasada de antes do reset não autoriza nada. | `reset-capture`, `reset-payment` |

## Entregáveis

Entregue na forma anunciada pela disciplina:

1. **`student/`**: código, `prj.conf`, `Kconfig`, overlays e as suas suítes
   de teste de unidade em `student/tests/`.
2. **Relatório** (PDF ou Markdown): máquina de estados e transições, mapa de
   pinos e interfaces, analisadores e novas tentativas, posse do ADC, exemplos
   de tarifa, falhas e recuperação, limitações conhecidas.
3. **Evidências**: a saída de `scripts/grade.py` e dos cenários, com IDs de
   execução e tempos virtuais. Mantenha as falhas; capturas de tela sozinhas
   não bastam.
4. **`student/REGISTRO_IA.md`** preenchido (modelo no próprio arquivo).

## Avaliação

A nota tem **portões**, que precisam passar todos, e **pontos**, que somam
100 e viram uma nota de 0 a 10.

### Portões

| Portão | Como é verificado | Se falhar |
|---|---|---|
| G1 Build | `student/` compila em uma árvore limpa do curso | não avaliado |
| G2 Sem leituras fabricadas | Revisão do código: drivers reais, nada de escrever no estado dos modelos | não avaliado |
| G3 Segurança | Os cenários `obstruction-during-close`, `jam`, `contradictory-limits` e `missing-lower` passam | não avaliado |
| G4 Registro de IA | `student/REGISTRO_IA.md` preenchido e atribuições de código de terceiros | não avaliado |
| G5 Defesa oral | Conversa curta, **sem agente**: você explica um requisito e faz uma pequena mudança ao vivo | a nota fica limitada a 5,0 |

"Não avaliado" significa que a entrega volta para correção, conforme a regra
de reenvio da disciplina.

### Pontos

| Item | Pontos | Como pontua |
|---|---:|---|
| Cenários públicos | 20 | proporcional aos cenários que passam |
| Cenários ocultos | 20 | proporcional aos cenários que passam |
| Cobertura de linhas | 30 | ≥ 95%: 30; entre 85% e 95%: linear; < 85%: 0 |
| Análise estática | 10 | 10 menos 2 por achado, mínimo 0 |
| Relatório e projeto | 20 | rubrica: estados, posse do ADC, analisadores, tarifa, falhas, limitações |

**Nota = pontos ÷ 10**, limitada a 5,0 se a defesa oral falhar.

### O que cada métrica mede

- **Cobertura de linhas** ([detalhes](grading-tools.md#firmware-line-coverage)):
  fração das linhas executáveis de `student/src` executadas no firmware real,
  dentro do Renode, por algum cenário **que passa** ou por alguma das suas
  suítes de teste de unidade **que passa**. Na avaliação contam os cenários
  públicos, os ocultos e as suas suítes. Uma linha que só um cenário ou uma
  suíte reprovada executa não conta.
- **Análise estática** ([detalhes](grading-tools.md#static-check)): o GCC do
  SDK com `-fanalyzer` e um conjunto fixo de avisos. Conta cada aviso em
  `student/src`.

Os cenários sozinhos não alcançam tudo: nenhum deles faz um driver do Zephyr
falhar. Os caminhos de erro (`if (rc != 0)`, `device_is_ready`, argumentos
inválidos) se cobrem com **testes de unidade** (próxima seção). Uma linha que
nem os modelos nem um teste conseguem alcançar pode ser excluída com
`/* LCOV_EXCL_LINE: motivo */`, até 3% das linhas e com justificativa na
defesa.

### Testes de unidade (ztest + FFF)

Cada pasta `student/tests/<suíte>/` com um `CMakeLists.txt` é uma suíte
[ztest](https://docs.zephyrproject.org/latest/develop/test/ztest.html)
compilada para a mesma placa e executada no Renode pelas ferramentas do curso.
O [FFF](https://docs.zephyrproject.org/latest/develop/test/fff.html)
(`zephyr/fff.h`) cria funções falsas que registram as chamadas e devolvem o
valor que o teste escolher.

`student/tests/unit/` é um exemplo que compila e passa. Para usá-lo no seu
projeto:

1. Separe a lógica (estados, analisadores, tarifa, sessões) da camada fina
   que chama os drivers, por exemplo `hw_motor_ligar()` em `src/hw.c`.
2. Na suíte, compile os módulos de lógica (`target_sources(app PRIVATE
   ../../src/tarifa.c)`), **não** o `main.c` nem a camada de hardware.
3. Substitua a camada de hardware por falsos do FFF
   (`FAKE_VALUE_FUNC(int, hw_motor_ligar, bool)`) e teste os dois lados:
   o caminho normal e o erro (`hw_motor_ligar_fake.return_val = -EIO`).

Os testes de unidade complementam os cenários, não os substituem: os portões
e os pontos de cenários continuam medindo o firmware inteiro.

### Escore de mutação: retorno, sem nota

`scripts/mutation-test.py` ([detalhes](grading-tools.md#mutation-score))
cria cópias do seu código com um erro pequeno cada (`<` vira `<=`, `&&` vira
`||`, uma constante muda, uma chamada some…) e roda os testes de unidade e os
cenários que passam pela linha alterada. Um mutante que **sobrevive** aponta
uma regra que nenhum teste verifica. Não vale nota; use para descobrir onde
falta um teste. Cobrir uma linha não basta: o teste precisa verificar o
resultado.

## Como se autoavaliar

Com o ambiente do [setup](setup.md) ativo, na raiz do repositório:

```sh
python scripts/grade.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/nota-01
```

O comando compila `student/`, roda todos os cenários públicos e as suas
suítes de unidade, mede cobertura e análise estática e imprime portões e
pontos. Os itens manuais (G2, G4, G5 e o relatório) aparecem como pendentes.
Leva de 5 a 10 minutos; `--jobs N` usa mais núcleos.

Para iterar mais rápido, rode cada ferramenta sozinha:

```sh
python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
  --output build/normal-01 --headless normal          # um cenário
python scripts/firmware-coverage.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/cobertura-01
python scripts/mutation-test.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/mutacao-01 \
  --coverage build/cobertura-01 --sample 20 --seed 7   # retorno, sem nota
python scripts/static-check.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/estatica-01
```

O seu resultado é um **limite inferior**: a avaliação acrescenta cenários
ocultos, que só podem aumentar a cobertura. Eles também podem reprovar o seu
firmware. A forma de se proteger é cumprir os requisitos,
não os cenários públicos.

Cada execução cria uma pasta nova em `build/`. Apague as antigas quando não
precisar mais delas.

## Trabalhando com um agente de IA

Você pode usar um agente (Claude Code, Codex, Copilot…) para construir o
projeto, dentro da política de IA da disciplina. O arquivo
[`AGENTS.md`](../AGENTS.md), na raiz, passa as regras deste desafio ao agente.
Ele é lido automaticamente pela maioria dos agentes; o `CLAUDE.md` aponta para
ele.

O agente pode escrever código, mas **quem é avaliado é você**:

- **A defesa oral é sem agente.** Você precisa explicar qualquer parte do
  código entregue e fazer uma pequena mudança ao vivo, como trocar um prazo,
  um limiar ou uma regra de rearme. Peça ao agente que explique cada decisão
  e anote o que você não entendeu.
- **Valide pelos periféricos**, não pela conversa. "Implementei e funciona"
  vale só depois de um cenário passar. Peça ao agente para rodar os cenários
  e mostrar o resultado.
- **Registre em `student/REGISTRO_IA.md`** a ferramenta e o modelo, o que
  pediu, o que aceitou ou rejeitou e como verificou.
- **Não envie** material privado, credenciais ou o link de capacidade do
  laboratório para serviços externos.

Bons pedidos para o agente: "explique o REQ-05 com os seus próprios exemplos",
"escreva a máquina de estados da entrada antes de codificar", "rode
`firmware-coverage.py` e explique cada linha não coberta", "que teste de
unidade com FFF cobre o erro deste driver?", "qual mutante sobreviveu e que
asserção o mataria?".
