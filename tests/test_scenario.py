"""Host-only strict parser, schema and behavioral oracle regression controls."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from scenario.parser import ScenarioError, load, loads, SIGNALS, STATE
from scenario.schema import schema
from scenario.assessment import FirmwareFailure, HarnessFailure, Stream, assess, checkpoint
from scenario.robot_library import ScenarioLibrary, charge_uuids
from scenario.outcome import infrastructure_error
from test_parking_observations import cycle, event


def fixture():
    return load(ROOT / "tests/scenarios/stationary.json")


def parse(data):
    return loads(json.dumps(data).encode())


class ParserTests(unittest.TestCase):
    def test_all_required_fixtures_parse(self):
        files = sorted((ROOT / "tests/scenarios").glob("*.json"))
        self.assertGreaterEqual(len(files), 19)
        for path in files:
            with self.subTest(path=path):
                self.assertEqual(load(path)["name"], path.stem)

    def test_schema_uses_parser_scalar_rules(self):
        exported = schema()
        self.assertEqual(exported["$schema"], "https://json-schema.org/draft/2020-12/schema")
        run = exported["$defs"]["run"]
        self.assertFalse(run["additionalProperties"])
        variants = run["properties"]["actions"]["items"]["oneOf"]
        for signal, (low, high, step) in SIGNALS.items():
            variant = next(v for v in variants if
                           v["properties"].get("signal", {}).get("const") == signal)
            self.assertEqual(variant["properties"]["value"],
                             {"type": "integer", "minimum": low, "maximum": high, "multipleOf": step})
        self.assertEqual(set(exported["$defs"]["state"]["properties"]), set(STATE))

    def test_exported_draft_schema_and_fixtures(self):
        try:
            from jsonschema import Draft202012Validator
        except ImportError:
            self.skipTest("optional jsonschema dependency not installed")
        exported = schema()
        Draft202012Validator.check_schema(exported)
        validator = Draft202012Validator(exported)
        for path in sorted((ROOT / "tests/scenarios").glob("*.json")):
            with self.subTest(path=path):
                validator.validate(load(path))
        invalid = fixture()
        invalid["runs"][0]["actions"] = [
            {"at_tick": 1, "kind": "submit", "signal": "vehicle.speed", "value": 1}]
        self.assertFalse(validator.is_valid(invalid))

    def test_unknown_fields_rejected_at_every_object(self):
        original = fixture()
        original["runs"][0]["actions"] = [
            {"at_tick": 1, "kind": "submit", "signal": "sensor.noise", "value": 0}]
        def objects(value):
            if isinstance(value, dict):
                yield value
                for child in value.values():
                    yield from objects(child)
            elif isinstance(value, list):
                for child in value:
                    yield from objects(child)
        for index in range(len(list(objects(original)))):
            data = copy.deepcopy(original)
            list(objects(data))[index]["unknown"] = 1
            with self.subTest(index=index), self.assertRaises(ScenarioError):
                parse(data)

    def test_json_ambiguity_and_resource_limits(self):
        cases = [
            b'{"version":1,"version":1}', b'{"runs":[{"x":1,"x":2}]}',
            b"NaN", b"Infinity", b"\xff", b"{} {}",
            b"[" * 13 + b"0" + b"]" * 13, b" " * 65537,
        ]
        for raw in cases:
            with self.subTest(raw=raw[:80]), self.assertRaises(ScenarioError):
                loads(raw)

    def test_wrong_types_and_ranges(self):
        for value in (True, False, 1.0, "1", None, -1, 2147483648):
            data = fixture()
            data["seed"] = value
            with self.subTest(value=value), self.assertRaises(ScenarioError):
                parse(data)
        for signal, (low, high, step) in SIGNALS.items():
            for value in (low - 1, high + 1, True, 0.0, "0"):
                data = fixture()
                data["runs"][0]["actions"] = [
                    {"at_tick": 1, "kind": "submit", "signal": signal, "value": value}]
                with self.subTest(signal=signal, value=value), self.assertRaises(ScenarioError):
                    parse(data)

    def test_timing_bounds_and_counts(self):
        for mutate in (
            lambda d: d.update(version=2),
            lambda d: d["deadlines"].update(motor_ms=9999),
            lambda d: d.update(runs=d["runs"] * 9),
            lambda d: d["runs"][0].update(duration_ticks=6001),
            lambda d: d["runs"][0].update(actions=[
                {"at_tick": 1, "kind": "submit", "signal": "sensor.noise", "value": 0}] * 257),
            lambda d: d["runs"][0].update(expect=d["runs"][0]["expect"] * 129),
            lambda d: d["runs"][0]["expect"][0].update(at_tick=901),
            lambda d: d["runs"][0]["green"][0].update(after_tick=600, before_tick=500),
            lambda d: d["runs"][0].update(actions=[
                {"at_tick": 2, "kind": "submit", "signal": "sensor.noise", "value": 0},
                {"at_tick": 1, "kind": "submit", "signal": "sensor.noise", "value": 0}]),
            lambda d: d["runs"][0].update(actions=[
                {"at_tick": 1, "kind": "monitor", "command": "quit"}]),
        ):
            data = fixture()
            mutate(data)
            with self.assertRaises(ScenarioError):
                parse(data)

    def test_nested_config_limits(self):
        config = dict(plate="ABC1D23", confidences=[93], delay_ticks=10,
                      fragment_bytes=8, missing=False)
        for key, value in (("plate", 'ABC1D23" quit'), ("confidences", []),
                           ("confidences", [93] * 17), ("confidences", [101]),
                           ("delay_ticks", 0), ("fragment_bytes", 129), ("missing", 1)):
            data = fixture()
            data["runs"][0]["camera"] = {**config, key: value}
            with self.subTest(key=key), self.assertRaises(ScenarioError):
                parse(data)


def source(events):
    result = []
    for index, e in enumerate(events):
        result.append({**e, "v": 1, "run": 1, "seq": index + 1,
                       "tick": max(1, e["time_us"] // 10000)})
    return result


class AssessmentTests(unittest.TestCase):
    def test_replay_uuids_reject_firmware_monitor_injection(self):
        uuid = "00000000-0000-8000-8000-000000000001"
        good = f"P1 CHARGE {uuid} ABC1D23 200 BRL"
        invalid = [
            "P1 CHARGE", "P1 CHARGE x", f"P1 CHARGE {uuid}",
            'P1 CHARGE ";quit ABC1D23 200 BRL',
            'P1 CHARGE "\\";quit ABC1D23 200 BRL',
            f"P1 CHARGE {uuid} ABC1D23 0200 BRL",
            f"P1 CHARGE {uuid} ABC1D23 1000001 BRL",
            f"P1 CHARGE {uuid} ABC1D23 200 USD",
            f"P1 CHARGE {uuid} ABC1D23 200 BRL\nquit",
            f"P1 CHARGE {uuid} ABC1D23 200 BRL\n",
            f"P1 CHARGE {uuid} ABC1D23 200 BRL\\",
            f"P1 CHARGE {uuid.upper()} ABC1D23 200 BRL".replace("8000", "ABCD", 1),
        ]
        self.assertEqual(charge_uuids(invalid), [])
        self.assertEqual(charge_uuids([*invalid, good, good]), [uuid])

    def test_every_green_not_just_last(self):
        physical, protocol = cycle()
        p, q = source(physical), source(protocol)
        windows = [{"after_tick": 0, "before_tick": 100, "payment_required": True}]
        assess(p, q, {"time_us": 40}, windows, final=True)
        # Earlier premature transition must not be hidden by later good output.
        q.insert(0, {**q[-1], "time_us": 0})
        with self.assertRaises(FirmwareFailure):
            assess(p, q, {"time_us": 40}, windows)

    def test_motor_continuous_trailing_and_completed_intervals(self):
        on = event(100000, "enable", 1, "motor", "commanded")
        off = event(2620000, "enable", 0, "motor", "commanded")
        for events, end in (([on], 2620000), ([on, off], 2700000)):
            with self.subTest(events=events), self.assertRaisesRegex(FirmwareFailure, "motor_deadline"):
                assess(events, [], {"time_us": end}, [])
        assess([on], [], {"time_us": 2610000}, [])

    def test_reversal_or_endpoint_does_not_restart_deadline(self):
        physical = [event(0, "enable", 1, "motor", "commanded"),
                    event(2000000, "direction", 0, "motor", "commanded"),
                    event(2300000, "position_mdeg", 0)]
        with self.assertRaisesRegex(FirmwareFailure, "motor_deadline"):
            assess(physical, [], {"time_us": 2600000}, [])

    def test_bad_earlier_interval_not_hidden_by_later_good_one(self):
        physical = [
            event(0, "enable", 1, "motor", "commanded"),
            event(2600000, "enable", 0, "motor", "commanded"),
            event(2700000, "enable", 1, "motor", "commanded"),
            event(2800000, "enable", 0, "motor", "commanded")]
        with self.assertRaisesRegex(FirmwareFailure, "motor_deadline"):
            assess(physical, [], {"time_us": 2800000}, [])

    def test_missing_or_extra_green_and_payment(self):
        physical, protocol = cycle()
        p, q = source(physical), source(protocol)
        window = [{"after_tick": 0, "before_tick": 100, "payment_required": True}]
        with self.assertRaisesRegex(FirmwareFailure, "unexpected_green"):
            assess(p, q, {"time_us": 40}, [])
        with self.assertRaisesRegex(FirmwareFailure, "green_count"):
            assess([], [], {"time_us": 40}, window, final=True)
        q = [e for e in q if e["device"] != "payment"]
        with self.assertRaisesRegex(FirmwareFailure, "green_authorization"):
            assess(p, q, {"time_us": 40}, window)

    def test_checkpoint_is_actual_state(self):
        with self.assertRaises(FirmwareFailure):
            checkpoint({"enable": False}, {"enable": True, "time_us": 123}, {}, [])
        with self.assertRaises(FirmwareFailure):
            checkpoint({"position_max_mdeg": 89999},
                       {"position_mdeg": 90000, "time_us": 123}, {}, [])

    def test_ring_duplicates_gaps_and_run_boundaries(self):
        e = source([event(100, "position_mdeg", 0)])[0]
        stream = Stream(1, 100)
        stream.collect([e])
        stream.collect([e])
        self.assertEqual(len(stream.events), 1)
        for bad in ({**e, "run": 2}, {**e, "seq": 3}, {**e, "time_us": 99}):
            with self.assertRaises(HarnessFailure):
                stream.collect([bad])

    def test_robot_writes_harness_failure_on_bad_input(self):
        library = ScenarioLibrary()
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "assessment.json"
            with self.assertRaises(AssertionError):
                library.execute_scenario(Path(tmp) / "missing.json", "boot.resc", target)
            result = json.loads(target.read_text())
            self.assertEqual(result["status"], "harness_failure")


class RunnerTests(unittest.TestCase):
    """Fake official-runner process tests; these do not execute Renode."""

    def invoke(self, name, timeout=10, following=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            renode = root / "renode"
            (renode / "tests").mkdir(parents=True)
            (renode / "tests/run_tests.py").write_text(
                "import json,os,sys,time\n"
                "from pathlib import Path\n"
                "variables=dict(sys.argv[i+1].split(':',1) for i,arg in "
                "enumerate(sys.argv) if arg=='--variable')\n"
                "name=json.loads(Path(variables['SCENARIO']).read_text())['name']\n"
                "if name=='timeout': time.sleep(60)\n"
                "if name=='cancel': os.kill(os.getppid(),15); time.sleep(60)\n"
                "if name=='silent': sys.exit(0)\n"
                "status={'pass':'pass','firmware':'firmware_failure',"
                "'harness':'harness_failure','teardown':'pass'}[name]\n"
                "Path(variables['SCENARIO_RESULT']).write_text(json.dumps({'status':status}))\n"
                "results=Path(sys.argv[sys.argv.index('--results-dir')+1]);results.mkdir()\n"
                "wanted='FAIL' if status=='firmware_failure' else 'PASS'\n"
                "xml='<robot><suite><kw name=\"Setup\" type=\"SETUP\"><status status=\"PASS\"/>'"
                "'</kw><test><kw name=\"Execute Scenario\"><status status=\"'+wanted+'\"/></kw>'"
                "'<status status=\"'+wanted+'\">'+('firmware_failure: fault' if wanted=='FAIL' else '')"
                "+'</status></test></suite></robot>'\n"
                "(results/'robot_output.xml').write_text(xml)\n"
                "sys.exit(0 if name=='pass' else 1)\n")
            machine = root / "boot.resc"
            machine.write_text("# Fake host test only\n")
            data = fixture()
            data["name"] = name
            scenario = root / "input.json"
            scenario.write_text(json.dumps(data))
            paths = [str(scenario)]
            if following:
                extra = root / "second.json"
                extra.write_text(json.dumps({**data, "name": following}))
                paths.append(str(extra))
            output = root / "evidence"
            process = subprocess.run(
                [sys.executable, str(ROOT / "scripts/scenario-run.py"),
                 "--renode-root", str(renode), "--machine-script", str(machine),
                 "--output", str(output), "--timeout", str(timeout), *paths],
                capture_output=True, text=True, timeout=20)
            result = json.loads(process.stdout)
            metadata = json.loads(next(output.glob("*/runner.log.json")).read_text())
            frozen = json.loads(next(output.glob("*/scenario.json")).read_text())
            self.assertEqual(frozen, data)
            return process.returncode, result, metadata

    def test_status_classification_and_no_stale_pass(self):
        for name, expected, code in (
            ("pass", "pass", 0), ("firmware", "firmware_failure", 1),
            ("harness", "harness_failure", 2), ("silent", "harness_failure", 2),
            ("teardown", "harness_failure", 2),
        ):
            with self.subTest(name=name):
                actual, result, metadata = self.invoke(name)
                self.assertEqual(actual, code)
                self.assertEqual(result["status"], expected)
                self.assertFalse(metadata["timed_out"])

    def test_host_watchdog_is_finite_and_retains_metadata(self):
        code, result, metadata = self.invoke("timeout", timeout=1)
        self.assertEqual(code, 2)
        self.assertEqual(result["status"], "harness_failure")
        self.assertTrue(metadata["timed_out"])
        self.assertEqual(metadata["returncode"], 124)

    def test_cancellation_does_not_launch_following_scenario(self):
        code, result, metadata = self.invoke("cancel", following="pass")
        self.assertEqual(code, 2)
        self.assertEqual(result["status"], "harness_failure")
        self.assertEqual(len(result["results"]), 1)
        self.assertEqual(metadata["cancellation_signal"], 15)

    def test_preflight_failure_is_durable_and_existing_output_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "bad.json"
            source.write_text('{"version":1,"version":1}')
            output = root / "evidence"
            command = [
                sys.executable, str(ROOT / "scripts/scenario-run.py"),
                "--renode-root", str(root), "--machine-script", str(root / "boot.resc"),
                "--output", str(output), str(source)]
            process = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(process.returncode, 2)
            saved = (output / "results.json").read_bytes()
            self.assertEqual(json.loads(saved)["status"], "harness_failure")
            self.assertIn("duplicate JSON key", json.loads(saved)["reason"])
            subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual((output / "results.json").read_bytes(), saved)

    def test_failed_teardown_overrides_firmware_failure(self):
        for kind in ("SETUP", "TEARDOWN"):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                xml = (
                    '<robot><suite><test>'
                    '<kw name="Execute Scenario"><status status="FAIL"/></kw>'
                    f'<kw name="Cleanup" type="{kind}"><status status="FAIL">transport</status></kw>'
                    '<status status="FAIL">firmware_failure: motor_deadline</status>'
                    '</test></suite></robot>')
                (root / "robot_output.xml").write_text(xml)
                self.assertIn("failed", infrastructure_error(root, "firmware_failure"))

    def test_missing_xml_never_accepts_a_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIn("exactly one", infrastructure_error(Path(tmp), "pass"))


if __name__ == "__main__":
    unittest.main()
