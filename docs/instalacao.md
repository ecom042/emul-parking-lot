# Instalação do laboratório

Este guia prepara um computador com **Linux** (x86_64 ou aarch64) para
compilar o firmware e rodá-lo no Renode. A referência técnica, em inglês, é o
[setup](setup.md); se os dois divergirem, vale o setup.

O Windows nativo não é suportado. O WSL 2 com Ubuntu 24.04 deve funcionar
com este mesmo guia, mas ainda não foi validado.

Faça tudo como usuário comum, sem `sudo`, exceto a instalação dos pacotes do
sistema. Nada aqui altera o firewall nem exporta o Zephyr globalmente.

## Memória e disco

Cada processo do Renode usa até cerca de **2 GB de RAM**. Um cenário por vez
roda com 4 GB no total; as ferramentas de medição rodam vários em paralelo e
escolhem sozinhas quantos cabem (até 4, um a cada 2 GB). Para forçar menos,
use `--jobs 1`.

| Item | Tamanho aproximado |
|---|---:|
| Zephyr SDK 1.0.1 (mínimo + toolchain ARM) | 0,8 GB |
| Renode 1.17.0 | 0,2 GB |
| Dependências do west (`deps/`) | 1,4 GB |
| Ambiente Python (`.venv/`) | 0,75 GB |
| Cada execução em `build/` | 10 a 60 MB |

Reserve uns 5 GB. Apague as pastas antigas de `build/` quando não precisar
mais delas.

## Pré-requisitos

Validado em **Ubuntu 24.04**. Em outras distribuições, instale os
equivalentes dos mesmos pacotes; o que importa é ter **Python 3.12**, Git,
CMake ≥ 3.20, Ninja e dtc.

## 1. Pacotes do sistema

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends git cmake ninja-build \
  device-tree-compiler python3 python3-venv python3-dev wget xz-utils \
  file ca-certificates unzip
python3.12 --version
```

No Ubuntu 22.04 ou no Debian 12 não há `python3.12` nos pacotes padrão:
instale um Python 3.12 à parte antes do passo 4.

## 2. Zephyr SDK 1.0.1

O pacote mínimo tem só as ferramentas de instalação; `setup.sh -t` baixa a
toolchain ARM (cerca de 100 MB). O `sha256sum -c` precisa imprimir `OK`.

```sh
cd ~
A=$(uname -m)        # x86_64 ou aarch64
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/zephyr-sdk-1.0.1_linux-${A}_minimal.tar.xz
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/sha256.sum
grep "zephyr-sdk-1.0.1_linux-${A}_minimal.tar.xz" sha256.sum | sha256sum -c -
tar xf zephyr-sdk-1.0.1_linux-${A}_minimal.tar.xz
rm zephyr-sdk-1.0.1_linux-${A}_minimal.tar.xz sha256.sum
cd zephyr-sdk-1.0.1
./setup.sh -t arm-zephyr-eabi
gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc --version   # 14.3.0
```

## 3. Renode 1.17.0

Use o pacote **portátil** oficial: ele já traz o .NET e não precisa de
instalação. Escolha o arquivo e o SHA-256 da sua arquitetura:

| `uname -m` | Arquivo | SHA-256 |
|---|---|---|
| `x86_64` | `renode-1.17.0.linux-portable.tar.gz` | `4ba7c68b59e2447f188ef4b4b112fcccd2802582c460beedceb63b84e5605a5f` |
| `aarch64` | `renode-1.17.0.linux-arm64-portable.tar.gz` | `e9051578411441bfb47b77de0381cfba3badce03663367fab561472fd7807f1f` |

```sh
cd ~
F=renode-1.17.0.linux-portable.tar.gz          # aarch64: renode-1.17.0.linux-arm64-portable.tar.gz
wget https://github.com/renode/renode/releases/download/v1.17.0/$F
echo "4ba7c68b59e2447f188ef4b4b112fcccd2802582c460beedceb63b84e5605a5f  $F" | sha256sum -c -
mkdir renode-1.17.0
tar xf $F -C renode-1.17.0 --strip-components=1
rm $F
~/renode-1.17.0/renode --version
```

A saída precisa conter `build: 1.17.0+20260906gitf1dd1b4af`. Um Renode
instalado pelo `apt`, por um `.deb` ou por outro caminho no `PATH` não serve:
o `lab.py` confere essa versão e recusa as outras.

## 4. Espaço de trabalho

A disciplina entrega o laboratório em dois arquivos `.zip`. Extraia os dois
na mesma pasta nova e entre no projeto. O caminho pode ter espaços, mas não
aspas, ponto e vírgula nem barra invertida.

```sh
mkdir ~/estacionamento && cd ~/estacionamento
unzip /caminho/emul-parking-lot.zip
unzip /caminho/zephyr-virtual-board.zip
cd emul-parking-lot
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-tools.txt
west init -l manifest
west update --narrow
python -m pip install -r deps/zephyr/scripts/requirements.txt
python -m pip check
```

O `west update` baixa o Zephyr e o HAL do STM32 para `deps/` e leva alguns
minutos. Faça isso uma vez só. Não rode `west zephyr-export`.

## 5. Variáveis de cada sessão

Todo terminal precisa do ambiente Python e de três variáveis. Grave-as em
`~/estacionamento/ambiente.sh`, ajustando os caminhos se instalou em outro
lugar:

```sh
cat > ~/estacionamento/ambiente.sh <<'FIM'
LAB="$HOME/estacionamento/emul-parking-lot"
. "$LAB/.venv/bin/activate"
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-1.0.1"
export RENODE_ROOT="$HOME/renode-1.17.0"
export ZVB_MODULE="$LAB/deps/zephyr-virtual-board"
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

## 6. Primeiro teste

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

## 7. Visualização no navegador

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
| `initialize the documented local west workspace first` | Falta rodar `west init -l manifest` e `west update --narrow` no projeto. |
| `output exists` | Escolha uma pasta de saída nova em `build/`. |
| `supported hosts: …` | Sistema ou arquitetura sem Renode e SDK oficiais, como Windows nativo. |

Estes passos foram validados do zero em Ubuntu 24.04 aarch64, num contêiner
com 2 GB de RAM: instalação, cenários, testes de unidade e navegador. Em
x86_64 os passos são os mesmos, mudando só os arquivos baixados.
