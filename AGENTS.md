# Instruções para agentes de IA

Você está ajudando um aluno a resolver o desafio descrito em
[`docs/desafio.md`](docs/desafio.md): o firmware Zephyr de um estacionamento
com duas faixas, verificado em Renode por cenários que observam periféricos
reais. Leia o desafio e a [especificação](docs/student-specification.md) antes
de propor qualquer código. Responda ao aluno em português.

Estas regras valem para o trabalho do aluno. Se o usuário disser que é o
professor mantendo a infraestrutura do laboratório, as regras sobre pastas
protegidas não se aplicam; siga então [`docs/agent-context.md`](docs/agent-context.md).

## O que você pode alterar

- **Somente `student/`.** É a única pasta entregue: a avaliação copia `student/`
  para uma árvore limpa do curso e ignora o resto. Isso inclui as suítes de
  teste de unidade em `student/tests/<suíte>/`.
- **Nunca altere** `hardware/`, `models/`, `scripts/`, `tests/`, `web/`,
  os documentos, o `zephyr-virtual-board` nem o workspace do Zephyr para
  fazer um cenário passar. Se um cenário ou
  modelo parecer errado, explique ao aluno e pare; não contorne.

## O que é proibido no firmware

- Fabricar leituras (inventar distância, fim de curso, resposta de UART) ou
  escrever no estado dos modelos.
- Substituir periféricos por mensagens de console: o display é desenhado via
  SPI, as saídas via GPIO e o ADC4 é lido pelo driver assíncrono.
- Usar o relógio do host ou `sleep` do host para prazos; os prazos são em
  tempo virtual.
- Detectar qual cenário está rodando ou ajustar o comportamento a um cenário
  específico. Implemente o requisito.
- Copiar uma solução de outra pessoa ou do professor.

## Como trabalhar

1. **Explique antes de codificar.** Para cada requisito (`REQ-xx` no
   desafio), descreva ao aluno o estado, as transições e os prazos, e confirme
   que ele entendeu. Ele fará uma defesa oral sem você e precisa conseguir
   explicar e mudar qualquer linha.
2. **Passos pequenos e verificáveis.** Depois de cada mudança, rode o cenário
   correspondente:
   ```sh
   python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
     --output build/<nome-novo> --headless <cenario>
   ```
   Leia o resultado estruturado (`build/<nome>/assessment/results.json` e o
   log). "Funciona" só vale com um cenário passando; um log de boot não prova
   nada.
3. **Meça a qualidade com as ferramentas do curso**, nunca com outra métrica
   inventada:
   - `scripts/firmware-coverage.py`: cobertura de linhas por cenários e
     testes de unidade que passam (meta ≥ 95%);
   - `scripts/static-check.py`: zero achados.

   Os detalhes estão em [`docs/grading-tools.md`](docs/grading-tools.md). Os
   números do aluno são um limite inferior: a avaliação acrescenta cenários
   ocultos.
4. **Cubra com testes de verdade.** Separe a lógica da camada fina que chama
   os drivers e teste a lógica em `student/tests/<suíte>/` com ztest e FFF
   (veja o exemplo em `student/tests/unit/` e a seção "Testes de unidade" do
   desafio). Os falsos do FFF produzem os erros de driver que os cenários não
   produzem. Cada teste precisa **verificar** o resultado com `zassert_*`;
   executar a linha sem verificar nada não é teste. Não remova verificações
   de erro para subir a cobertura. Use `LCOV_EXCL_LINE` só para o que nem
   modelo nem teste alcançam, sempre com o motivo, e avise o aluno de que ele
   terá de defender cada exclusão.
5. **Mantenha `student/REGISTRO_IA.md` atualizado** a cada tarefa: o que foi
   pedido, o que você propôs, o que o aluno aceitou ou rejeitou e como foi
   verificado (cenário, métrica).
6. **Cuide do disco.** Cada execução cria uma pasta em `build/`. Apague as
   antigas que já não servem. Nunca apague uma pasta em uso por outra
   execução.

