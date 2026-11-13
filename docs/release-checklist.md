# Release checklist

## Scope

Use this checklist for each source distribution or classroom rollout. It is not
a test result or permission to publish a release.

Project-owned source/documentation use Apache-2.0. The lab selects a published
ZVB commit through the manifest; retain that immutable identity and its notices.
Changing source or documentation changes archive bytes: create new candidates
and manifests rather than relabeling an earlier archive.

## Exact local artifact workflow

```sh
python scripts/package-lab.py --module "$ZVB_MODULE" --output build/release-candidate-01
```

Retain its manifest and SHA-256 map alongside both source archives. They
identify actual source bytes, not just a module base SHA or dirty flag.
Git HEAD can be absent in extracted source; the per-file hashes remain required.
Archive names and hashes identify candidate bytes, not release approval; do not overwrite them.
Extract to a new directory, verify every digest, and rerun the setup/pilot.
Archives contain student source, simulator infrastructure and public assessment;
instructor solutions and probes must be excluded. Do not include credentials,
capability links, cookies, build evidence, SDK/runtime binaries or downloaded
dependencies.

### Verify and extract a local candidate

Use only a manifest obtained from the trusted instructor/review source.
Matching hashes prove byte identity, not the authenticity of an untrusted
manifest. From the source root, after creating the candidate above:

```sh
python - build/release-candidate-01 build/pilot-01 <<'PY'
import hashlib, json, sys, zipfile
from pathlib import Path
package, output = map(Path, sys.argv[1:])
manifest = json.loads((package / "manifest.json").read_text())
output.mkdir(parents=True, exist_ok=False)
for source in manifest["sources"].values():
    archive = package / source["archive"]
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == source["archive_sha256"]
    with zipfile.ZipFile(archive) as bundle:
        expected = {item["archive_path"] for item in source["files"]}
        assert set(bundle.namelist()) == expected
        for item in source["files"]:
            path = Path(item["archive_path"])
            assert not path.is_absolute() and ".." not in path.parts
            data = bundle.read(item["archive_path"])
            assert len(data) == item["size"]
            assert hashlib.sha256(data).hexdigest() == item["sha256"]
        bundle.extractall(output)
print("Verified and extracted local candidate; no publication performed.")
PY
```

Then follow setup from the extracted `emul-parking-lot/`, selecting its sibling
`zephyr-virtual-board/`. For a nested pilot use setup's explicit absolute
manifest initialization; never remove the ancestor workspace. The automated
launch audit is:

```sh
python scripts/bounded-command.py --timeout 220 --output build/pilot-launch.log -- \
  python scripts/check-student-launch.py --module "$ZVB_MODULE" \
  --renode "$RENODE_ROOT/renode" --output build/pilot-launch-01
```

This builds/launches the actual inert student app, authenticates in memory,
observes source-time progress and restricted listeners, then verifies exit,
runtime absence and HTTP refusal. It never saves the capability or cookie.
Use `lab.py --headless normal` for the initial starter's expected firmware
failure. Instructors can run completed and faulty controllers separately; never
include those implementations in the student artifact.
Use full module/runtime/output arguments as in setup.

## Required gates and retained limits

- [ ] Verify the curated student distribution excludes instructor solutions,
  probes and solution-specific validation helpers, even if local copies exist.
  Student setup must not depend on the instructor material.
- [ ] Review the selected source for solution content. A path allowlist cannot
  detect an instructor implementation copied into an otherwise allowed file.
- [ ] Publish only explicitly approved student refs. Never push local instructor
  branches, recovery refs or bundles using `--all` or `--mirror`.
- [ ] Review the exact source being distributed and record its commit and any
  local modifications.
- [ ] Verify the manifest resolves an accessible immutable ZVB revision with the
  required runtime/model assets.
- [ ] Include the project Apache-2.0 `LICENSE`; preserve third-party terms.
- [ ] Retain ZVB `LICENSE` (Apache-2.0) and applicable upstream notices.
  Zephyr/HAL/CMSIS, Renode, SDK, generator and Python/browser dependencies keep
  their own licenses/notices. Review redistribution terms before bundling.
  Do not bundle SDK/browser or third-party reference PDFs by assumption.
- [ ] Capture source archive/wheel hashes, file map, base commits and local
  additions; review packaged assets.
- [ ] Repeat fresh-venv/fresh-build/extracted-artifact setup on macOS arm64.
  Existing SDK/tools are prerequisites, not clean OS evidence.
- [ ] Review transitive Python/PEP 517 inputs and remotely fetched
  `STM32U5XX.svd.gz` bytes/provenance. Direct pins and `pip freeze` are not a
  complete lock or offline reproduction guarantee.
- [ ] Review the automated clean-source pilot evidence. A human pilot without
  undocumented help is recommended before classroom rollout but not claimed
  by automation; instructor approves duration/weights/AI policy, not tooling.
- [ ] Build the starter independently and verify inert outputs and UI/lifecycle
  behavior. Validate completed-controller scenarios with separately maintained
  instructor firmware, without distributing that solution.
- [ ] Repeat Host/Origin/session auth, bounded SSE/reconnect/UART suffix,
  runtime no-network, HTTP `127.0.0.1` and lifecycle checks.
- [ ] No debugger, raw monitor dashboard or official Robot listener in normal
  student workflow.

Linux/Windows/Intel macOS remain unsupported pending actual tests. Retain the
`nucleo_u5a5zj_q`/ADC4 subset, fixed-clock/RCC/UART/SPI limitations, nondurable
sessions (reset is a lab restart), car-queue runs of at most one virtual hour,
simulated money and same-timestamp
cross-owner observation limitation. Do not expand scope to resolve these as
part of packaging.
