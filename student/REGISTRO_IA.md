# Registro de uso de IA

Preencha durante o trabalho, não no fim. É o portão G4 da avaliação
([desafio](../docs/desafio.md#portões)): os dois alunos da dupla precisam ter
entradas. Quem não usou IA escreve isso aqui.

## Dupla

| Aluno | Matrícula | Partes que escreveu |
|---|---|---|
| | | |
| | | |

## Ferramentas

| Aluno | Ferramenta | Modelo/versão | Período de uso |
|---|---|---|---|
| Pablo | Codex | GPT-6 | 18/11/2026 |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-18 — Base do controlador

- **Aluno** Pablo.
- **Requisitos** REQ-01, REQ-03, REQ-06 a REQ-10 e pontos de integração
  para REQ-02, REQ-04, REQ-05 e REQ-13.
- **Pedido** Analisar os contratos e implementar o núcleo das duas faixas,
  com GPIO/EXTI, ADC4 assíncrono e interfaces para os módulos da dupla.
- **Proposta da IA** Usar duas instâncias de `lane.c`, concentrar as saídas
  em `parking_gpio.c`, isolar a aquisição em `parking_adc.c` e trocar pedidos
  e resultados correlacionados pelo contrato de `parking.h`.
- **Aceitação ou alterações** Aguardando revisão do aluno.
- **Verificação** Passaram 14 testes da lógica em C no Windows com GCC e FFF,
  usando asserções C para executar os casos da suíte. A análise local do núcleo
  com `-fanalyzer` terminou sem diagnósticos. Build da placa, ztest no Renode,
  cenários e métricas do curso pendentes.
- **Para a sabatina** Explicar o caminho EXTI, thread de controle e motor
  desligado. Explicar por que a memória da conversão continua válida após timeout.

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| | | |
