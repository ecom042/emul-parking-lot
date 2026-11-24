# Skills usadas pela dupla

Skills do Claude Code usadas no projeto e como instalá-las. Elas não fazem
parte do firmware nem da avaliação.


## `drawio` (plugin oficial do draw.io, não copiado)

Gera diagramas `.drawio` (a partir de Mermaid ou XML) e os exporta para PNG,
SVG ou PDF. Foi usada para os diagramas de [`../diagrams/`](../diagrams/). Ela
não está copiada aqui porque é de terceiros (`jgraph/drawio-mcp`) e só
funciona instalada como plugin.

**Instalação**, dentro do Claude Code:

```text
/plugin marketplace add jgraph/drawio-mcp
/plugin install drawio@drawio
/reload-plugins
```

**Dependência:** o **draw.io desktop** precisa estar instalado e no `PATH`
(`which drawio`). Sem ele, o plugin não converte Mermaid nem exporta imagens,
e o agente cai para escrever o XML do diagrama à mão. No Linux, instale o
pacote `.deb` ou o snap de <https://github.com/jgraph/drawio-desktop/releases>.

**Para editar os diagramas sem o Claude Code:** abra os `.drawio` no draw.io
desktop ou no VS Code com a extensão *Draw.io Integration*
(`hediet.vscode-drawio`). Os `.drawio.png` também guardam o diagrama
embutido e podem ser abertos no draw.io para edição.
