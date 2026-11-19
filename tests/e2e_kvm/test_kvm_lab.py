"""US1: bring-up, idempotence, mode exclusion, error paths and teardown of the KVM lab.

Order matters: each test starts from the lab state it needs (contract lab-kvm.md, quickstart K1-K16).
"""
import importlib.util
import os
import re
import subprocess
import tempfile
import time
from pathlib import Path

import pytest

from conftest import ROOT, common, guest_pids, netns_names, run_lab

KVM_DIR = ROOT / "build" / "lab" / "kvm"
_iv = importlib.util.spec_from_file_location("mvr_kvm_image", ROOT / "lab" / "kvm" / "image.py")
image = importlib.util.module_from_spec(_iv)
_iv.loader.exec_module(image)


def lock(key):
    for line in (ROOT / "lab" / "kvm" / "image.lock").read_text().splitlines():
        if line.startswith(key + "="):
            return line.split("=", 1)[1]


def mvr_ns():
    return [n for n in netns_names() if n.startswith("mvr-")]


@pytest.fixture(scope="module", autouse=True)
def clean_start():
    run_lab("down")
    assert mvr_ns() == []
    yield


def test_k15_no_kvm_device_exits_3_fast_and_creates_nothing():
    t0 = time.monotonic()
    r = run_lab("up", "--kvm", env={"MINIVR_KVM_DEVICE": "/nonexistent"})
    assert r.returncode == 3
    assert time.monotonic() - t0 < 5
    assert "Virtualize Intel VT-x/EPT" in r.stderr
    assert mvr_ns() == []


def test_unusable_kvm_device_exits_4_and_suite_reports_fail_not_skip():
    with tempfile.TemporaryDirectory() as d:
        dev = Path(d) / "kvm"
        dev.write_text("")
        os.chmod(dev, 0o600)  # owned by root: the developer cannot open it
        env = {"MINIVR_KVM_DEVICE": str(dev)}
        r = run_lab("up", "--kvm", env=env)
        assert r.returncode == 4
        assert "not accessible" in r.stderr
        assert mvr_ns() == []
        e = dict(os.environ, **env)
        r = subprocess.run([str(ROOT / "tests" / "e2e" / "run.sh"), "--kvm"], capture_output=True,
                           text=True, env=e, timeout=300)
        assert r.returncode != 0
        assert "SKIP" not in r.stdout


def test_corrupted_image_is_deleted_and_reported():
    with tempfile.TemporaryDirectory() as d:
        img = Path(d) / lock("CIRROS_FILE")
        img.write_bytes(b"not a disk image")
        r = run_lab("up", "--kvm", env={"MINIVR_KVM_CACHE": d})
        assert r.returncode == 1
        assert "image checksum mismatch (deleted); re-run to download again" in r.stderr
        assert not img.exists()


def test_kvm_up_refused_while_namespace_lab_is_up():
    assert run_lab("up").returncode == 0
    try:
        before = mvr_ns()
        r = run_lab("up", "--kvm")
        assert r.returncode == 1
        assert "namespace lab is up; run 'sudo make lab-clean' first" in r.stderr
        assert mvr_ns() == before and "mvr-mgmt" not in before
    finally:
        run_lab("down")


def test_not_ready_names_guest_and_console_log():
    r = run_lab("up", "--kvm", env={"MINIVR_KVM_BOOT_TIMEOUT": "1"})
    assert r.returncode == 1
    assert re.search(r"vm[123] not ready after 1s; console: build/lab/kvm/logs/vm[123]\.console\.log",
                     r.stderr), r.stderr
    run_lab("down")


def test_k1_up_ready_in_time_with_ports():
    t0 = time.monotonic()
    r = run_lab("up", "--kvm")
    elapsed = time.monotonic() - t0
    assert r.returncode == 0, r.stderr
    assert elapsed < 90, f"bring-up took {elapsed:.0f}s"
    st = run_lab("status").stdout
    for vm in ("vm1", "vm2", "vm3"):
        assert re.search(rf"^{vm}\s+ready\s", st, re.M), st
    # the image layer is applied: no DHCP wait, so the console shows the patched network start
    assert "Starting network: OK (static" in (KVM_DIR / "logs" / "vm1.console.log").read_text()
    links = common.ns_exec("mvr-cn1", "ip", "-j", "link", "show").stdout
    for p in ("p1", "p2", "p3"):
        assert f'"ifname":"{p}"' in links.replace(" ", "")


def test_k2_second_up_keeps_the_guests():
    before = guest_pids()
    assert len(before) == 3
    assert run_lab("up", "--kvm").returncode == 0
    assert guest_pids() == before


def test_k3_plain_up_refused_while_kvm_is_up():
    before = guest_pids()
    r = run_lab("up")
    assert r.returncode == 1
    assert "KVM lab is up; run 'sudo make lab-clean' first" in r.stderr
    assert guest_pids() == before


def test_k16_guest_memory_within_budget():
    total_kb = 0
    for pid in guest_pids():
        m = re.search(r"VmRSS:\s+(\d+) kB", Path(f"/proc/{pid}/status").read_text())
        total_kb += int(m.group(1))
    assert total_kb <= 1024 * 1024, f"guests use {total_kb // 1024} MB"


def test_k14_teardown_leaves_nothing_behind():
    run_lab("down")
    snap_after = common.host_snapshot()
    assert guest_pids() == []
    assert not Path("/run/mvr-kvm").exists()
    assert mvr_ns() == []
    if KVM_DIR.exists():
        assert sorted(p.name for p in KVM_DIR.iterdir()) == ["logs"]
    assert not any(n.startswith("mvr-") for n in (l["ifname"] for l in snap_after["link"]))
    # full before/after equality of the host snapshot is asserted by the session guard


def test_k17_tampered_layer_is_rebuilt_and_golden_untouched():
    """The layer is verified before every bring-up: damage is detected and the layer rebuilt."""
    lock = image.read_lock(ROOT / "lab" / "kvm" / "image.lock")
    cache = ROOT / "build" / "cache" / "images"
    golden = cache / lock["CIRROS_FILE"]
    layer = cache / image.layer_name(lock["CIRROS_VERSION"], lock["MVR_REV"],
                                     lock["MVR_LAYER_LOGICAL_SHA256"])
    run_lab("down")
    golden_sha = image.sha256_file(golden)
    assert golden_sha == lock["CIRROS_SHA256"]
    with open(layer, "r+b") as f:
        f.seek(3_000_000)
        original = f.read(4)
        f.seek(3_000_000)
        f.write(bytes(b ^ 0xFF for b in original))
    r = run_lab("up", "--kvm")
    assert r.returncode == 0, r.stderr
    with open(layer, "rb") as f:
        f.seek(3_000_000)
        assert f.read(4) == original, "layer was not rebuilt"
    assert image.sha256_file(golden) == golden_sha, "the golden image must never be written"
