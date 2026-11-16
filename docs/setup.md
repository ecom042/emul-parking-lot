# Local laboratory setup

This is the student launch workflow. The manifest ([`west.yml`](../west.yml))
selects Zephyr v4.5.0 and published ZVB revision
`aed53b3cedad1e93d6a92837ce79d8cd7198d2af`, containing the reviewed model,
stdio and HTTP/SSE fixes; `west update` fetches both. An attached checkout or
unpublished patch is no longer necessary.
Project-owned source and documentation use [Apache-2.0](../LICENSE).
The [release checklist](release-checklist.md) retains the classroom and
reproducibility qualifications.

## Linux host and prerequisites

Students use **Linux, x86_64 or aarch64**; the Portuguese
[installation guide](instalacao.md) gives these steps in Portuguese.
Both architectures have an official Renode 1.17.0 portable build and a Zephyr
SDK 1.0.1. `lab.py` refuses other hosts, such as native Windows.
Clean-installation evidence exists for Ubuntu 24.04 aarch64 in a container;
x86_64 is not yet runtime-validated (see
[agent context](agent-context.md#linux-host-validation)). Commands here do not
alter a firewall or export Zephyr globally. A supported browser must run
locally; Chrome or Chromium is the tested browser.

Required: the host dependencies of Zephyr v4.5.0's
[Getting Started guide](https://docs.zephyrproject.org/4.5.0/develop/getting_started/index.html)
and Python **3.12**. Zephyr itself and the Zephyr SDK **1.0.1** (`arm-zephyr-eabi`
toolchain, GNU Arm GCC 14.3.0) come from the lab workspace below.
Validated with the Ubuntu 24.04 packages (Python 3.12.3, CMake 3.28.3, Ninja
1.11.1, dtc 1.7.0); these are evidenced versions, not a claim that others
fail. About 6 GB of disk (workspace and SDK) and 2 GB of RAM per parallel Renode process.

Use the official [Renode 1.17.0 portable release](https://github.com/renode/renode/releases/tag/v1.17.0),
which bundles .NET 8 and reports build `1.17.0+20260906gitf1dd1b4af`
(`lab.py` checks it). Distribution packages (`.deb`, `.rpm`) and PATH
wrappers are not the selected runtime.

| `uname -m` | Asset | SHA-256 |
|---|---|---|
| `x86_64` | `renode-1.17.0.linux-portable.tar.gz` | `4ba7c68b59e2447f188ef4b4b112fcccd2802582c460beedceb63b84e5605a5f` |
| `aarch64` | `renode-1.17.0.linux-arm64-portable.tar.gz` | `e9051578411441bfb47b77de0381cfba3badce03663367fab561472fd7807f1f` |

Extract it into a directory of its own, set `RENODE_ROOT` to it and check
`"$RENODE_ROOT/renode" --version`: an old PATH wrapper can return zero while
printing an error.

## Workspace bootstrap

The lab uses west **topology 2**: this repository is the manifest repository,
and `west update` places Zephyr, `hal_stm32`, `cmsis_6` and
`zephyr-virtual-board` beside it in the workspace folder. Use a new folder
outside any other west workspace. Ordinary spaces in supported input paths are
tested; quotes, semicolons, backslashes and other whitespace are rejected.
Internet access is required for Python, Git dependencies and the generator's
remote SVD. Students receive only the `emul-parking-lot` archive; no Git HEAD
is required in it.

```sh
mkdir ~/estacionamento && cd ~/estacionamento
unzip /path/to/emul-parking-lot.zip
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install -r emul-parking-lot/requirements-tools.txt
west init -l emul-parking-lot
west update --narrow
west packages pip --install
west sdk install -t arm-zephyr-eabi
python -m pip check
```

The resulting layout is `.venv/`, `.west/`, `emul-parking-lot/`,
`modules/hal/{stm32,cmsis_6}`, `zephyr/` and `zephyr-virtual-board/`.
`west packages pip --install` installs Zephyr's and the modules' Python
requirements into the venv; `west sdk install` puts the SDK in
`~/zephyr-sdk-1.0.1` (skip it when that SDK is already installed). Every new
shell needs the venv and `ZEPHYR_SDK_INSTALL_DIR`, `RENODE_ROOT` and
`ZVB_MODULE` (the workspace's `zephyr-virtual-board/`); the
[installation guide](instalacao.md#3-variáveis-de-cada-sessão) keeps them in
one file to source. Do not run `west zephyr-export`.

`lab.py` finds the workspace from its own location (the closest `.west/`
above this repository), sets `WEST_CONFIG_LOCAL` and `ZEPHYR_BASE` from it
(ignoring inherited values) and checks that `zephyr/` is exactly v4.5.0, so a
workspace not updated after a manifest change is refused. It explicitly selects
the reviewed module's Python assembly and transport without modifying the
workspace, nested source or global packages. It rejects missing reviewed
runtime files. Presence checks do not replace instructor artifact verification.
`lab.py` is a command-line process, not an in-process library API: it selects
its own cwd/environment (discarding inherited ZVB/west/Python source overrides),
then replaces itself with the existing UI launcher. Those changes affect only
that CLI process and its descendants, not the invoking shell, machine settings,
ancestor workspace or persistent west configuration. Imported helper functions
are used by host tests; calling `main()` inside another application is not a
supported integration contract.
Direct bootstrap pins are in `requirements-tools.txt`; the generator Git pin
is exact, and `source.json` records the Zephyr path and commit of each build. Upstream Python requirement ranges, PEP 517 build
inputs, SDK/host distributions and remote SVD bytes are **not a complete hash
lock**. `pip freeze` records one environment, not a portable lock.

## Build, launch and inspect

```sh
python scripts/lab.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/lab-01
```

This builds `student/` for `nucleo_u5a5zj_q` into a new output directory,
generates the platform and trusted boot/reset through ZVB's existing shared
machine assembly, then starts the existing restricted bridge/UI and official
Renode. There is no alternate physics implementation or raw monitor route.
Open the **private local capability link printed on your terminal**.
Do not redirect the launch terminal into shared logs or copy this link into
screenshots. The browser exchanges it for a session cookie and removes the
fragment. Only HTTP binds `127.0.0.1`; the runtime uses private pipes.
Debugger and legacy raw dashboard stay disabled.

The dashboard opens on the car queue: choose Start and the queued cars drive
to the entry barrier one by one, park for their configured seconds, then leave
through the exit lane. Select a car to change how it behaves, or use Add car.
The starter deliberately keeps both lanes' motor/green/buzzer off and red on;
it does **not** recognize/pay/open, so every car is refused with a failed
verdict until you implement the controller. Implement those
behaviors using real Zephyr drivers. Canvas pixels come from actual SPI;
an unimplemented display is blank, not a setup failure. Diagnostics contain
bounded GPIO/UART/SPI observations. A complete current snapshot is authoritative
state, not a claim that all previous UART bytes remain in its recent window.
Truncated history is explicitly marked.

Ctrl-C stops/reaps the owned runtime and HTTP service; closing a tab does not.
Reset coherently reloads the same ELF and clears sessions/pending equipment
responses. Each launch uses a **new** output (`build/lab-02`, etc.); no evidence
or previous build is deleted. Rebuild/relaunch after firmware edits: live code
reload is not supported. Preserve `source.json`, build/prepare logs and runtime
evidence, but never session capabilities/cookies.

The interactive build uses a 5000 ms tariff unit so stays of a few virtual
seconds show different prices; headless fixtures build the documented 1000 ms
unit. Add `--capacity N` (1..8) to build a different lot size.

For build/preparation only, add `--prepare-only`. For a browser-free **public**
scenario on your implementation, use the same workflow:

```sh
python scripts/lab.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/student-normal-01 \
  --headless normal
```

This reuses the listener-free private-stdio adapter and existing behavioral
oracles. `--headless all` runs public fixtures sequentially, stopping on the
first failure/incomplete run; later fixtures are then unassessed. An unfinished
starter is expected to fail behavior, not produce an infrastructure pass.
Read the structured result and source events; nonzero status alone does not
tell whether firmware or setup failed. The older official Robot assessment
path is instructor-only privileged infrastructure, not this workflow.

## Local firmware and trust assumptions

Edit `student/src/` and its CMake/Kconfig as necessary. Public
`hardware/parking.overlay` and `hardware/renode/parking.repl` define the common
wiring used by student firmware and the simulator. Preserve those interfaces and the
fixed clock/ADC4 subset; no ADC1, DMA or general display driver is supported.
Custom local projects must retain equivalent public wiring, ZVB bundle,
equipment timeline and common models. Do not modify the Zephyr workspace, the
`zephyr-virtual-board` module, models or assessment expectations to make a
submission pass.

This is not protection against a person editing their local simulator or
running malicious native code. Instructor assessment uses trusted frozen
infrastructure; emulator ELF/model/extension inputs are trusted local code.
Inspect submissions and use institutional isolation procedures where needed.
The **normal student API** cannot set motor outputs, raw ADC, barrier position
or send monitor commands. No real payment or PIX is involved.

## Pilot reproducibility procedure

Use `scripts/package-lab.py` to create local source artifacts and their file/
archive hashes, then extract them into a new empty worktree-local directory.
The [release checklist](release-checklist.md#verify-and-extract-a-local-candidate)
provides the exact verification/extraction command.
Follow bootstrap above with a new venv and no inherited `PYTHONPATH`.
Do not copy build outputs or compiled ELF. Record exact dependency commits,
`pip freeze`, `pip check`, host/SDK/runtime versions, build hashes, launch/
authentication/teardown evidence. Confirm inert starter outputs and an expected
normal-scenario firmware failure before implementing the assignment. Instructors
can validate the environment against a separately maintained completed controller;
that code is not a student dependency or part of this package.
A second person should follow these instructions
without undocumented help and report unclear steps and time spent; no teaching
duration or schedule is inferred from machine build time.

Automated artifact/host checks complement, not replace, that human pilot.
Record which steps actually ran and whether dependencies were downloaded or
read-copied from known caches; retain those results with the pilot's build output.

Pilot in a new folder outside any west workspace (not beneath another
workspace's `build/`): `west init -l` refuses to create a workspace inside
another one. Do not delete that ancestor.
