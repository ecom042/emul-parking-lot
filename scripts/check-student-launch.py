"""Check the initial safe starter build/launch on macOS; retain no capabilities."""
import argparse
import http.client
import json
import os
from pathlib import Path
import selectors
import signal
import socket
import subprocess
import sys
import time
from urllib.parse import urlsplit


def network_audit(pid):
    os.kill(pid, 0)
    result = subprocess.run(["lsof", "-nP", "-a", "-p", str(pid), "-i"],
                            capture_output=True, text=True, timeout=10)
    os.kill(pid, 0)
    if result.returncode not in (0, 1) or result.stderr:
        raise RuntimeError("network audit failed; no containment conclusion")
    return {"pid": pid, "command": result.args, "returncode": result.returncode,
            "stdout": result.stdout, "stderr": result.stderr}


def overall_status(record):
    return ("pass" if record["status"] == "checks_passed" and record["returncode"] == 0
            and record.get("runtime_absent_after_close")
            and record.get("http_closed_after_close") else "fail")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--module", type=Path, required=True)
    ap.add_argument("--renode", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = [sys.executable, str(root / "scripts/lab.py"), "--module",
               str(args.module.resolve()), "--renode", str(args.renode.resolve()),
               "--output", str(output / "lab")]
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    record = {"command": command, "status": "pending"}
    runtime_pid = None
    url = None
    try:
        deadline, buffer, url = time.monotonic() + 180, b"", None
        while time.monotonic() < deadline and url is None:
            if proc.poll() is not None:
                raise RuntimeError("launcher exited before private URL")
            for key, _ in selector.select(0.2):
                buffer += os.read(key.fileobj.fileno(), 4096)
                if len(buffer) > 8192:
                    raise RuntimeError("unexpected launcher output size")
                for line in buffer.decode("utf-8", errors="replace").splitlines():
                    if line.startswith("http://127.0.0.1:") and "#token=" in line:
                        url = urlsplit(line)
        if url is None:
            raise RuntimeError("launcher bootstrap deadline")
        origin = f"http://{url.netloc}"
        cookie = None

        def request(method, path, body=None, headers=None):
            connection = http.client.HTTPConnection(url.hostname, url.port, timeout=5)
            fields = {"Host": url.netloc, "Origin": origin, **(headers or {})}
            if cookie:
                fields["Cookie"] = cookie
            if body is not None:
                fields["Content-Type"] = "application/json"
                body = json.dumps(body)
            try:
                connection.request(method, path, body, fields)
                response = connection.getresponse()
                return response.status, response.getheader("Set-Cookie"), response.read()
            finally:
                connection.close()

        assert request("GET", "/api/snapshot")[0] == 401
        status, session, _ = request("POST", "/api/session",
                                      headers={"X-ZVB-Token": url.fragment.split("=", 1)[1]})
        assert status == 204 and session
        cookie = session.split(";", 1)[0]
        status, _, data = request("GET", "/api/snapshot")
        assert status == 200
        state = json.loads(data)
        assert request("POST", "/api/command", {
            "v": 1, "run": state["run"], "id": "pilotstart", "op": "start"})[0] == 200
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            state = json.loads(request("GET", "/api/snapshot")[2])
            if state["physical"]["tick"] >= 25:
                break
            time.sleep(0.2)
        assert state["physical"]["tick"] >= 25 and not state["failure"]
        assert not state["physical"]["enable"] and not state["protocol"]["green"]
        assert state["protocol"]["red"]
        rows = subprocess.check_output(
            ["ps", "-axo", "pid=,ppid=,command="], text=True).splitlines()
        children = [row.split(None, 2) for row in rows
                    if len(row.split(None, 2)) == 3 and row.split(None, 2)[1] == str(proc.pid)]
        runtime_pid = next(int(row[0]) for row in children if str(args.renode.resolve()) in row[2])
        audits = {}
        for name, pid in (("runtime", runtime_pid), ("http", proc.pid)):
            audits[name] = network_audit(pid)
        assert audits["runtime"]["returncode"] == 1 and not audits["runtime"]["stdout"]
        assert not audits["runtime"]["stderr"] and not audits["http"]["stderr"]
        assert f"{url.netloc} (LISTEN)" in audits["http"]["stdout"]
        assert "*:" not in audits["http"]["stdout"]
        record.update(status="checks_passed", origin=origin, snapshot=state, network=audits)
    finally:
        selector.close()
        proc.send_signal(signal.SIGTERM) if proc.poll() is None else None
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
            record["status"] = "teardown_timeout"
        record["returncode"] = proc.returncode
        if runtime_pid:
            try:
                os.kill(runtime_pid, 0)
                record["runtime_absent_after_close"] = False
            except ProcessLookupError:
                record["runtime_absent_after_close"] = True
        if url:
            try:
                with socket.create_connection((url.hostname, url.port), timeout=2):
                    record["http_closed_after_close"] = False
            except ConnectionRefusedError:
                record["http_closed_after_close"] = True
        record["status"] = overall_status(record)
        (output / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    assert record["status"] == "pass" and record["returncode"] == 0
    assert record["runtime_absent_after_close"]
    print("Student CLI build/launch/auth/inert firmware/network/teardown passed.")


if __name__ == "__main__":
    main()
