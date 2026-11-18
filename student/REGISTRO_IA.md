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
| | | | |

## Tarefas

Uma entrada por tarefa relevante (um requisito, um bug, uma refatoração).

### 2026-11-18 — Interfaces e cenário inicial (issue #1)

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex, versão não informada.
- **Requisitos:** REQ-01 a REQ-13.
- **Pedido:** iniciar a issue #1 com a proposta de interfaces e a execução do cenário inicial.
- **Proposta:** divisão de responsabilidades, estados e prazos em `student/INTERFACES.md`.
- **Aceitação ou revisão:** pendente pela dupla.
- **Verificação:** a tentativa local x86_64 parou na verificação da versão do Renode. Em ARM64 emulado, o build e a preparação passaram, mas a inicialização expirou. A execução `issue-01-actions-normal` no GitHub Actions ARM64 nativo (job 44333386814) confirmou a falha esperada da base no tick 590, aos 5,90 s virtuais. Os resultados estão em `student/INTERFACES.md`.
- **Aprendizado da aluna:** a preencher após a revisão.

### 2026-11-18 — Segurança das cancelas (issue #2)

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex, versão não informada.
- **Requisitos:** REQ-01, REQ-06, REQ-08 e REQ-09.
- **Pedido:** seguir para a implementação da segurança das cancelas.
- **Proposta:** máquina de estados testável em `gate.c`, GPIO do devicetree e EXTI com trabalho diferido em `main.c`, e testes ztest/FFF para movimentos, obstrução e assistência.
- **Aceitação ou revisão:** a revisar pela dupla.
- **Verificação:** [job ARM64 44335948702](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44335948702). `partial-startup` e `contradictory-limits` passaram até o checkpoint de 6,00 s virtuais (tick 600). Os oito testes `gate_safety` passaram; a cobertura parcial foi de 82,84% das linhas e 67,42% dos branches, e a análise estática teve zero achados. Os cenários `jam`, `missing-lower` e `obstruction-during-close` ainda precisam da integração com reconhecimento e passagem para acionar o movimento no firmware.
- **Aprendizado da aluna:** a preencher após a revisão.

### 2026-11-18 — Validação de todos os cenários públicos

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Pedido:** rodar todos os cenários oficiais com o estado atual do firmware.
- **Verificação:** [job ARM64 44341017271](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44341017271), commit `abc3f08`. Passaram 2 de 19 cenários (`partial-startup` e `contradictory-limits`), a suíte unitária e a análise estática com zero achados. Cobertura de 82,84% das linhas e 67,42% dos branches.
- **Falhas observadas na issue #2:** `jam` esperava uma captura aos 6,00 s virtuais (tick 600) e observou zero. `missing-lower` e `obstruction-during-close` esperavam uma transição para verde e observaram zero. Esses cenários ainda não chegaram a exercitar a falha de movimento pretendida. O artefato do job preserva o resultado dos 19 cenários.

### 2026-11-18 — ADC4, presença, passagem e sessões

- **Aluna:** Maria Antonia Ribeiro Ramos.
- **Ferramenta:** Codex, versão não informada.
- **Pedido:** implementar as issues #3 e #4, preservando as atribuições do Jackson.
- **Proposta:** aquisição ADC4 assíncrona e controle de sessão e passagem, com adaptador para as operações de câmera e pagamento.
- **Verificação:** [job ARM64 44371350008](https://github.com/anntimar/estacionamento-embarcados/actions/runs/44371350008). O teste do driver ADC4 leu os dois canais no Renode; o teste `parking_flow` confirmou abandono, criação de sessão por passagem e liberação da vaga após a saída paga. Passaram as duas suítes e os cenários `partial-startup` e `contradictory-limits`. Cobertura parcial de 83,57% das linhas e 68,18% dos branches; zero achados na análise estática. Os cenários que exigem captura real, pagamento ou passagem completa aguardam o adaptador da dupla.
- **Aceitação e aprendizado:** a revisar pela aluna e pela dupla.

### AAAA-MM-DD — título curto

- **Aluno:** quem fez esta tarefa.
- **Requisito(s):** REQ-xx
- **O que pedi:** resumo do pedido.
- **O que a IA propôs:** resumo, arquivos e funções afetados.
- **O que aceitei, mudei ou rejeitei, e por quê:**
- **Como verifiquei:** cenários (nome e resultado), cobertura, testes de
  unidade, análise estática, revisão manual.
- **O que aprendi / o que ainda preciso entender:**

## Código de terceiros

Trechos copiados ou adaptados de outras fontes (documentação, exemplos do
Zephyr, fóruns), com origem e licença.

| Arquivo/trecho | Origem | Licença |
|---|---|---|
| | | |
