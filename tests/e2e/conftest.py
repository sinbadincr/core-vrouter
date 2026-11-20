"""Shared e2e fixtures: lab lifecycle, forwarder control, vrctl helper, host snapshot diff.

Everything here needs root (namespaces, raw sockets); without it the suite skips with a message.
"""
import json
import os
import subprocess
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BUILD = os.environ.get("MINIVR_BUILD", "debug")
LAB = ROOT / "lab" / "lab.sh"
VRCTL_BIN = ROOT / "build" / BUILD / "vrctl"
SOCK = "/run/mvr-cn1/minivr.sock"
TIME_BUDGET_S = int(os.environ.get("MINIVR_E2E_BUDGET_S", "180"))

# every vrctl call in the suite goes to the lab socket, never the default one
os.environ["MINIVR_SOCK"] = SOCK

_session_start = time.monotonic()


def pytest_collection_modifyitems(config, items):
    if os.geteuid() != 0:
        skip = pytest.mark.skip(reason="e2e needs root: run with sudo (namespaces, raw sockets)")
        for item in items:
            item.add_marker(skip)


def sh(*cmd, check=True, timeout=60, **kw):
    return subprocess.run([str(c) for c in cmd], check=check, timeout=timeout,
                          capture_output=True, text=True, **kw)


def lab_sh(*args, check=True, timeout=60):
    return sh(LAB, *args, check=check, timeout=timeout)


def host_snapshot():
    """Normalised root-namespace network state as a dict (lab/lab.sh snapshot)."""
    return json.loads(lab_sh("snapshot").stdout)


def ns_exec(ns, *cmd, check=True, timeout=60, **kw):
    return sh("ip", "netns", "exec", ns, *cmd, check=check, timeout=timeout, **kw)


def vrctl_raw(*args, check=False):
    """Run vrctl against the lab socket; returns the CompletedProcess (exit code checks)."""
    return sh(VRCTL_BIN, *args, check=check)


def vrctl(*args, json_out=True):
    """Run a vrctl command; returns parsed JSON (-j) or stdout text. Raises on exit != 0
    unless json_out, in which case an error response is returned as a dict with ok=false."""
    if json_out:
        r = sh(VRCTL_BIN, "-j", *args, check=False)
        if r.returncode not in (0, 1):
            raise RuntimeError(f"vrctl exit {r.returncode}: {r.stderr}")
        return json.loads(r.stdout)
    return sh(VRCTL_BIN, *args, check=True).stdout


def forwarder_pid():
    pidfile = ROOT / "build" / "lab" / "minivrd.pid"
    return int(pidfile.read_text()) if pidfile.exists() else None


def start_forwarder(config="cn1.conf"):
    """(Re)start minivrd in mvr-cn1 with a config from lab/. Returns its pid."""
    lab_sh("run", "--config", str(ROOT / "lab" / config))
    return forwarder_pid()


ARTIFACT_DIR = Path(os.environ.get("MINIVR_E2E_ARTIFACTS",
                                   ROOT / "build" / "e2e" / "artifacts" / time.strftime("%Y%m%d-%H%M%S")))


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    """On a failing test keep what is needed to debug it: forwarder log, tables, host snapshot."""
    outcome = yield
    rep = outcome.get_result()
    if rep.when != "call" or not rep.failed or os.geteuid() != 0:
        return
    d = ARTIFACT_DIR / item.name
    d.mkdir(parents=True, exist_ok=True)
    log = ROOT / "build" / "lab" / "minivrd.log"
    if log.exists():
        (d / "minivrd.log").write_text(log.read_text())
    for name, args in (("stats", ("stats", "show")), ("routes", ("route", "show")),
                       ("neighbours", ("neigh", "show")), ("ports", ("port", "show"))):
        r = subprocess.run([str(VRCTL_BIN), "-j", *args], capture_output=True, text=True)
        (d / f"{name}.json").write_text(r.stdout or r.stderr)
    r = subprocess.run([str(LAB), "snapshot"], capture_output=True, text=True)
    (d / "host-snapshot.json").write_text(r.stdout or r.stderr)
    (d / "lab-status.txt").write_text(subprocess.run([str(LAB), "status"], capture_output=True,
                                                     text=True).stdout)


@pytest.fixture(scope="session", autouse=True)
def _session_guard():
    """Fail the session if the host network state differs after the run or it overran its budget."""
    if os.geteuid() != 0:
        yield
        return
    lab_sh("down", check=False)
    before = host_snapshot()
    yield
    lab_sh("down", check=False)
    after = host_snapshot()
    assert before == after, "host network state changed during the e2e run"
    elapsed = time.monotonic() - _session_start
    assert elapsed <= TIME_BUDGET_S, f"e2e took {elapsed:.0f}s, budget {TIME_BUDGET_S}s"


@pytest.fixture()
def lab():
    """Lab is up (idempotent, cheap). Tests that need the forwarder use `forwarder`."""
    lab_sh("up", "--harness")
    yield


@pytest.fixture()
def forwarder(lab):
    """A fresh minivrd with lab/cn1.conf (zeroed counters); the session guard tears it down."""
    start_forwarder("cn1.conf")
    yield


PKT_TOOL = ROOT / "tests" / "e2e" / "pkt_tool.py"
VENV_PY = ROOT / ".venv" / "bin" / "python"


def pkt_tool(ns, *args, timeout=30):
    """Run tests/e2e/pkt_tool.py (scapy) inside namespace `ns`; returns parsed JSON output."""
    r = ns_exec(ns, VENV_PY, PKT_TOOL, *args, timeout=timeout)
    return json.loads(r.stdout.strip().splitlines()[-1])


def pkt_tool_bg(ns, *args):
    """Start pkt_tool in the background; returns the Popen (stdout line 1 is READY)."""
    p = subprocess.Popen(["ip", "netns", "exec", ns, str(VENV_PY), str(PKT_TOOL), *map(str, args)],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert p.stdout.readline().strip() == "READY", p.stderr.read()
    return p


def stats():
    return vrctl("stats", "show")["data"]


def reasons():
    return stats()["drops"]["reasons"]
