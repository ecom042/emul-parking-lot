# Local laboratory setup

This is the student launch workflow. The manifest selects published
ZVB revision `aed53b3cedad1e93d6a92837ce79d8cd7198d2af`, containing the reviewed
model, stdio and HTTP/SSE fixes. Use the module downloaded by `west update`;
an attached checkout or unpublished patch is no longer necessary.
Project-owned source and documentation use [Apache-2.0](../LICENSE).
The [release checklist](release-checklist.md) retains the classroom and
reproducibility qualifications.

## First supported host and prerequisites

Only **macOS arm64** has runtime evidence. Linux, Windows, Intel macOS,
containers and clean operating-system installations are not validated.
Install prerequisites through your institution's approved process; commands
here do not install global applications, alter a firewall or export Zephyr
globally. A supported browser must run locally; Chrome is the tested browser.
Use an existing **arm64 Zephyr SDK 1.0.1**, GNU Arm GCC **14.3.0**, Python
**3.12.16**, CMake **4.4.5**, Ninja **1.13.2**, dtc **1.8.1** and Git.
These are evidenced versions, not a claim that all other versions fail.
SDK downloads/toolchain selection must match host arm64 and target
`arm-zephyr-eabi`; an x86 host SDK is not interchangeable.

Check before setup (substitute the existing SDK path):

```sh
uname -s
uname -m
python3.12 --version
git --version
cmake --version
ninja --version
dtc --version
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --version
export ZEPHYR_SDK_INSTALL_DIR="/absolute/path/to/zephyr-sdk-1.0.1"
cat "$ZEPHYR_SDK_INSTALL_DIR/sdk_version"
file "$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc"
"$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" --version
```

Use the official [Renode 1.17.0 macOS arm64 portable release](https://github.com/renode/renode/releases/tag/v1.17.0),
reporting `1.17.0+20260906gitf1dd1b4af`, .NET 8.0.21. Download the arm64
portable DMG, verify it, mount it read-only, copy `Renode.app` into the
worktree-local directory below, and eject the image.
DMG SHA-256:
`63b1fb691207f503cea937e4ec8fad3e068a517d0abac3c24c0206c62f6c4d12`.
Keep its app in this worktree's ignored `build/official-renode/`, not a
replacement of `/Applications/Renode.app`. Verify actual version output;
an old PATH wrapper can return zero while printing an error.

```sh
shasum -a 256 /path/to/downloaded-renode.dmg
export RENODE_ROOT="$PWD/build/official-renode/Renode.app/Contents/MacOS"
"$RENODE_ROOT/renode" --version
```

## Workspace bootstrap

Run from this repository root. Ordinary spaces in supported input paths are
tested; quotes, semicolons, backslashes and other whitespace are rejected.
Internet access is required for Python, Git dependencies and the generator's
remote SVD. A student source archive has the same relative layout as this
repository; extract both archives into a new workspace and select its ZVB root.
No Git HEAD is required in the extracted module.

```sh
python3.12 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-tools.txt
west init -l manifest
west update --narrow
python -m pip install -r deps/zephyr/scripts/requirements.txt
python -m pip check
export ZVB_MODULE="$PWD/deps/zephyr-virtual-board"
```

`west init` is once per workspace; `.west/` and dependencies remain inside
this project. Do not run `west zephyr-export`. Build-time dependencies use the
published manifest for Zephyr/CMSIS/HAL; the launcher explicitly selects the
reviewed module's Python assembly and transport without modifying `.west/`,
the manifest, nested source or global packages. It rejects missing reviewed
runtime files. Presence checks do not replace instructor artifact verification.
`lab.py` is a command-line process, not an in-process library API: it selects
its own cwd/environment (discarding inherited ZVB/west/Python source overrides),
then replaces itself with the existing UI launcher. Those changes affect only
that CLI process and its descendants, not the invoking shell, machine settings,
ancestor workspace or persistent west configuration. Imported helper functions
are used by host tests; calling `main()` inside another application is not a
supported integration contract.
Direct bootstrap pins are in `requirements-tools.txt`; the generator Git pin
and Zephyr commit are exact. Upstream Python requirement ranges, PEP 517 build
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

Choose Start, request Car arrival, then Stop car near identification. The
starter deliberately keeps motor/green/buzzer off and red on; it does **not**
recognize/pay/open or pass completed-controller scenarios. Implement those
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
equipment timeline and common models; integration of an arbitrary external
Zephyr workspace is not automatic. Do not modify `deps/`, models or assessment
expectations to make a submission pass.

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

When piloting **inside another west workspace** (for example beneath its
ignored `build/`), `west init` initially discovers the ancestor `.west/` and
refuses. Do not delete that ancestor. Initialize the extracted workspace from
a directory outside any west workspace, supplying its absolute manifest:

```sh
P="/absolute/path/to/extracted/emul-parking-lot"
(cd / && env -u ZEPHYR_BASE "$P/.venv/bin/west" init -l "$P/manifest")
```

Then return to `$P` for update/install/build. This writes only the extracted
workspace's `.west/`, not its ancestor or `/`. Normal independent checkouts
use the bootstrap command above.
