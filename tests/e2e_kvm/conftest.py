"""Fixtures for the KVM lab suite (`sudo make e2e-kvm`). Needs root, KVM and the QEMU tools.

Skips when the host has no KVM (check-prereqs exit 3); FAILS when KVM exists but is not usable by
the developer or tools are missing (exit 4 / 2), so a broken setup never passes as a skip.
"""
import importlib.util
import os
import shutil
import subprocess
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

# reuse the phase 1 helpers (sh, lab_sh, vrctl, host_snapshot, ...) without their pytest hooks
_spec = importlib.util.spec_from_file_location("mvr_e2e_common", ROOT / "tests" / "e2e" / "conftest.py")
common = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(common)

LAB = common.LAB
LOGS = ROOT / "build" / "lab" / "kvm" / "logs"
TIME_BUDGET_S = int(os.environ.get("MINIVR_E2E_BUDGET_S", "300"))
ARTIFACT_DIR = common.ARTIFACT_DIR
_session_start = time.monotonic()
_prereq = {"rc": 0, "msg": ""}


def run_lab(*args, env=None, timeout=300):
    """lab.sh with extra environment; never raises on a non-zero exit."""
    e = dict(os.environ)
    e.update(env or {})
    return subprocess.run([str(LAB), *args], capture_output=True, text=True, timeout=timeout, env=e)


def pytest_collection_modifyitems(config, items):
    if os.geteuid() != 0:
        skip = pytest.mark.skip(reason="KVM e2e needs root: run with sudo")
        for item in items:
            item.add_marker(skip)
        return
    r = subprocess.run([str(ROOT / "scripts" / "check-prereqs.sh"), "--kvm"],
                       capture_output=True, text=True)
    _prereq["rc"], _prereq["msg"] = r.returncode, (r.stderr or r.stdout).strip()
    if r.returncode == 3:
        skip = pytest.mark.skip(reason=f"no KVM on this host: {_prereq['msg']}")
        for item in items:
            item.add_marker(skip)


@pytest.fixture(autouse=True)
def _kvm_host_usable():
    if _prereq["rc"] in (2, 4):
        pytest.fail(f"KVM host misconfigured (exit {_prereq['rc']}): {_prereq['msg']}")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    """On failure keep the guests' console logs, lab status and forwarder state."""
    outcome = yield
    rep = outcome.get_result()
    if rep.when != "call" or not rep.failed or os.geteuid() != 0:
        return
    d = ARTIFACT_DIR / item.name
    d.mkdir(parents=True, exist_ok=True)
    if LOGS.is_dir():
        for f in LOGS.iterdir():
            shutil.copy(f, d / f.name)
    (d / "lab-status.txt").write_text(run_lab("status").stdout)
    for name, args in (("stats", ("stats", "show")), ("neighbours", ("neigh", "show")),
                       ("ports", ("port", "show"))):
        r = subprocess.run([str(common.VRCTL_BIN), "-j", *args], capture_output=True, text=True)
        (d / f"{name}.json").write_text(r.stdout or r.stderr)


@pytest.fixture(scope="session", autouse=True)
def _session_guard():
    if os.geteuid() != 0 or _prereq["rc"] != 0:
        yield
        return
    run_lab("down")
    before = common.host_snapshot()
    yield
    run_lab("down")
    after = common.host_snapshot()
    assert before == after, "host network state changed during the KVM e2e run"
    elapsed = time.monotonic() - _session_start
    assert elapsed <= TIME_BUDGET_S, f"KVM e2e took {elapsed:.0f}s, budget {TIME_BUDGET_S}s"


@pytest.fixture(scope="module")
def kvm_lab():
    """KVM lab up (idempotent). Left up between modules; the session guard tears it down."""
    r = run_lab("up", "--kvm")
    assert r.returncode == 0, f"up --kvm failed ({r.returncode}):\n{r.stderr}"
    yield


@pytest.fixture(scope="module")
def forwarder(kvm_lab):
    """A fresh minivrd with lab/cn1.conf over the guests' TAPs."""
    r = run_lab("run")
    assert r.returncode == 0, r.stderr
    yield common.forwarder_pid()


def guest(vm, cmd, sudo=False, timeout=60, check=True):
    """Run `cmd` (a shell string) in guest `vm` over the management network."""
    r = run_lab("ssh", vm, ("sudo " if sudo else "") + cmd, timeout=timeout)
    if check:
        assert r.returncode == 0, f"{vm}: {cmd!r} exited {r.returncode}\n{r.stdout}\n{r.stderr}"
    return r


def guest_pids():
    r = subprocess.run(["pgrep", "-x", "mvr-vm[123]"], capture_output=True, text=True)
    return sorted(int(x) for x in r.stdout.split())


def netns_names():
    return [n.split()[0] for n in subprocess.run(["ip", "netns", "list"], capture_output=True,
                                                 text=True).stdout.splitlines() if n.strip()]
