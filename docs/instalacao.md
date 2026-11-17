# Instalação do laboratório

Este guia prepara um computador com **Linux** (x86_64 ou aarch64) para
compilar o firmware e rodá-lo no Renode. A referência técnica, em inglês, é o
[setup](setup.md); se os dois divergirem, vale o setup.

O Windows nativo não é suportado. O WSL 2 com Ubuntu 24.04 deve funcionar
com este mesmo guia, mas ainda não foi validado.

## Requisitos

- **Dependências do Zephyr.** Do
  [Getting Started oficial](https://docs.zephyrproject.org/4.5.0/develop/getting_started/index.html),
  faça só a instalação das dependências do sistema (CMake, Ninja, dtc,
  etc.). O Zephyr v4.5.0 e o Zephyr SDK 1.0.1 são instalados no passo 2.
- **Python 3.12.** No Ubuntu 24.04 é o `python3.12` do sistema.
- **Renode 1.17.0**, o pacote portátil oficial para Linux da
  [página da versão](https://github.com/renode/renode/releases/tag/v1.17.0)
  (`linux-portable` para x86_64, `linux-arm64-portable` para aarch64),
  extraído numa pasta própria. O `lab.py` só aceita esse Renode: um instalado
  pelo `apt` ou por um `.deb` é recusado.
- **Memória e disco**: cerca de 2 GB de RAM por processo do Renode (as
  ferramentas de medição rodam até 4 em paralelo e escolhem sozinhas quantos
  cabem; `--jobs 1` força um só) e uns 6 GB de disco (workspace e SDK).

## 1. Conferir

```sh
python3.12 --version
"$HOME/renode-1.17.0/renode" --version
```

Ajuste o caminho se instalou o Renode em outro lugar. Ele precisa mostrar
`build: 1.17.0+20260906gitf1dd1b4af`.

## 2. Espaço de trabalho

O laboratório é um workspace `west` (topologia 2): o projeto
`emul-parking-lot` traz o manifesto (`west.yml`), e o `west update` baixa ao
lado dele o Zephyr v4.5.0, o HAL do STM32, o CMSIS e o `zephyr-virtual-board`
(cerca de 1,5 GB). O `west init` clona o laboratório do
[repositório da disciplina](https://github.com/ecom042/emul-parking-lot). Quem entra no repositório privado que o
colega já criou troca a URL pela desse repositório (veja
[como entregar](desafio.md#como-entregar-repositório-privado-e-pull-request)). Use uma pasta nova,
fora de qualquer outro workspace `west`. O caminho pode ter
espaços, mas não aspas, ponto e vírgula nem barra invertida.

```sh
mkdir ~/estacionamento && cd ~/estacionamento
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install west
west init -m https://github.com/ecom042/emul-parking-lot --mr main .
python -m pip install -r emul-parking-lot/requirements-tools.txt
west update --narrow
west packages pip --install
west sdk install -t arm-zephyr-eabi
python -m pip check
```

O `west sdk install` instala o SDK em `~/zephyr-sdk-1.0.1`; se já o tiver,
pule esse comando. No fim, a pasta fica assim:

```text
~/estacionamento/
├── .venv/                  ambiente Python
├── emul-parking-lot/       o laboratório (você trabalha em student/)
├── modules/hal/            HAL do STM32 e CMSIS
├── zephyr/                 Zephyr v4.5.0
└── zephyr-virtual-board/   ferramentas do Renode
```

## 3. Variáveis de cada sessão

Todo terminal precisa do ambiente Python e de três variáveis. Grave-as em
`~/estacionamento/ambiente.sh`, ajustando os caminhos se instalou em outro
lugar:

```sh
cat > ~/estacionamento/ambiente.sh <<'FIM'
WS="$HOME/estacionamento"
LAB="$WS/emul-parking-lot"
. "$WS/.venv/bin/activate"
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-1.0.1"
export RENODE_ROOT="$HOME/renode-1.17.0"
export ZVB_MODULE="$WS/zephyr-virtual-board"
FIM
```

Em cada terminal novo, carregue o arquivo e vá para o projeto:

```sh
. ~/estacionamento/ambiente.sh
cd "$LAB"
```

Para carregá-lo sempre, acrescente a primeira linha ao `~/.bashrc`
(`echo '. ~/estacionamento/ambiente.sh' >> ~/.bashrc`). Os comandos a seguir
rodam na pasta do projeto.

## 4. Primeiro teste

```sh
python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
  --output build/normal-01 --headless normal
```

Com o código inicial, o firmware **compila e roda, mas o cenário falha**: o
código inicial nunca abre a cancela. O resultado em
`build/normal-01/assessment/results.json` deve mostrar:

```json
"failure": {
  "kind": "firmware_failure",
  "message": "checkpoint at 5.90 s (tick 590): position_mdeg expected 90000, observed 0"
}
```

Isso confirma que a instalação está certa. Um erro em `build.log`,
`prepare.log` ou `renode-version.log` dentro da pasta de saída indica um
problema de instalação, não do seu firmware.

## 5. Visualização no navegador

```sh
python scripts/lab.py --module "$ZVB_MODULE" --renode "$RENODE_ROOT/renode" \
  --output build/lab-01
```

Abra no navegador (Chrome ou Chromium) o **link privado que aparece no
terminal**. Ele só funciona na própria máquina (`127.0.0.1`). Não o copie
para capturas de tela nem para outras pessoas. Ctrl-C encerra o simulador.
A página está em português por padrão; o botão PT/EN no topo troca o idioma.

Cada execução precisa de uma pasta de saída nova (`build/lab-02`, …). Depois
de editar o firmware, rode o comando de novo: não há recarga automática.

## Problemas comuns

| Mensagem | Causa e solução |
|---|---|
| `use the documented Python 3.12 virtual environment` | O `.venv` não está ativo ou foi criado com outro Python. Ative-o ou recrie-o com Python 3.12. |
| `set ZEPHYR_SDK_INSTALL_DIR to the Zephyr SDK 1.0.1 for this host` | A variável não aponta para a pasta do SDK (a que contém `sdk_version`). |
| `Wrong runtime identity` | O `--renode` não é o Renode 1.17.0 oficial. Veja `renode-version.log`. |
| `initialize the west workspace first` | Falta o `west init -m …` e o `west update --narrow` do passo 2 em `~/estacionamento`. |
| `the workspace has Zephyr v…` ou `no Zephyr in …` | Rode `west update --narrow` em `~/estacionamento` (também depois de receber uma versão nova do laboratório). |
| `output exists` | Escolha uma pasta de saída nova em `build/`. |
| `supported hosts: …` | Sistema ou arquitetura sem Renode e SDK oficiais, como Windows nativo. |

Validado do zero em Ubuntu 24.04 aarch64, num contêiner com 2 GB de RAM,
seguindo este guia com o Zephyr v4.5.0: instalação, cenários, testes de
unidade e navegador.
