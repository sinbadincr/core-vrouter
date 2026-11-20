"""US4 + FR-014a/b: status, shell, key-only SSH, proof that data never uses the management path."""
import json
import re
import subprocess
import time
from pathlib import Path

import pytest

from conftest import LOGS, ROOT, common, guest, guest_pids, run_lab

pytestmark = pytest.mark.usefixtures("forwarder")
VENV_PY = ROOT / ".venv" / "bin" / "python"
SNIFF = ROOT / "tests" / "e2e_kvm" / "sniff_count.py"

GUESTS = {  # data-model.md
    "vm1": ("02:00:00:00:01:01", "10.0.1.11/24", "10.99.0.11"),
    "vm2": ("02:00:00:00:01:02", "10.0.1.12/24", "10.99.0.12"),
    "vm3": ("02:00:00:00:01:03", "10.0.2.13/24", "10.99.0.13"),
}


def port(name):
    return [p for p in common.vrctl("port", "show")["data"]["ports"] if p["name"] == name][0]


def wait_for(pred, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if pred():
            return True
        time.sleep(0.2)
    return pred()


def test_status_lists_guests():
    st = run_lab("status").stdout
    assert "== guests (mode kvm)" in st
    assert re.search(r"NAME\s+STATE\s+PID\s+PORT\s+MAC\s+DATA\s+MGMT\s+CONSOLE", st)
    for vm, (mac, data, mgmt) in GUESTS.items():
        row = next(l for l in st.splitlines() if l.startswith(vm + " "))
        assert "ready" in row and mac in row and data in row and mgmt in row, row


def test_ssh_shell_runs_command_and_leaves_guest_running():
    assert guest("vm2", "hostname").stdout.strip() == "vm2"
    assert re.search(r"^vm2\s+ready\s", run_lab("status").stdout, re.M)


def test_k12_password_login_refused():
    r = common.ns_exec("mvr-mgmt", "ssh", "-o", "PubkeyAuthentication=no", "-o",
                       "PreferredAuthentications=password", "-o", "BatchMode=yes", "-o",
                       "UserKnownHostsFile=/dev/null", "-o", "StrictHostKeyChecking=no",
                       "-o", "ConnectTimeout=5", "cirros@10.99.0.11", "true", check=False)
    assert r.returncode != 0 and "Permission denied" in r.stderr, r.stderr


@pytest.mark.parametrize("vm", sorted(GUESTS))
def test_guest_invariants(vm):
    mac, data, mgmt = GUESTS[vm]
    assert guest(vm, "cat /sys/class/net/eth0/address").stdout.strip() == mac
    a0 = guest(vm, "ip -4 -o addr show dev eth0").stdout
    a1 = guest(vm, "ip -4 -o addr show dev eth1").stdout
    assert data in a0 and mgmt + "/24" in a1
    routes = guest(vm, "ip route").stdout
    assert "default via" in routes and "dev eth0" in routes.split("default")[1].splitlines()[0]
    assert not [l for l in routes.splitlines() if l.startswith("default") and "eth1" in l]
    assert re.search(r"^10\.99\.0\.0/24 dev eth1\b", routes, re.M), routes  # connected route kept
    assert not [l for l in routes.splitlines() if "eth1" in l and not l.startswith("10.99.0.0/24")]
    assert guest(vm, "ip -6 addr show dev eth0; ip -6 addr show dev eth1").stdout.strip() == ""
    for other, (_, odata, _) in GUESTS.items():
        if other != vm:
            assert "dev eth0" in guest(vm, f"ip route get {odata.split('/')[0]}").stdout


def test_k11_no_data_packet_on_the_management_bridge():
    guest("vm2", "nohup sh -c 'nc -l -p 5002 >/dev/null' >/dev/null 2>&1 &")
    time.sleep(1)
    p = subprocess.Popen(["ip", "netns", "exec", "mvr-mgmt", str(VENV_PY), str(SNIFF), "br0",
                          "net 10.0.0.0/16"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    assert p.stdout.readline().strip() == "READY", p.stderr.read()
    guest("vm1", "sudo ping -c 200 -i 0.01 -W 1 10.0.2.13", timeout=60)
    guest("vm1", "dd if=/dev/zero bs=1M count=10 2>/dev/null | nc 10.0.1.12 5002", timeout=120)
    p.stdin.write("stop\n")
    p.stdin.flush()
    out = json.loads(p.stdout.readline())
    p.wait(timeout=10)
    assert out["frames"] == 0


def test_forwarder_counters_match_guest_traffic():
    guest("vm1", "ping -c 1 -W 2 10.0.2.13")  # resolve ARP first
    p1, p3 = port("p1"), port("p3")
    guest("vm1", "sudo ping -c 200 -i 0.01 -W 1 10.0.2.13", timeout=60)
    d1 = port("p1")["rx_pkts"] - p1["rx_pkts"]
    d3 = port("p3")["tx_pkts"] - p3["tx_pkts"]
    print(f"p1 rx delta {d1}, p3 tx delta {d3} (200 echo requests; the rest is ARP/other)")
    assert 200 <= d1 <= 210 and 200 <= d3 <= 210


def test_console_logs_exist_with_ready_marker():
    for vm in GUESTS:
        assert f"MVR-READY {vm}" in (LOGS / f"{vm}.console.log").read_text()


def test_k13_killed_guest_is_stopped_and_recovers(forwarder):
    subprocess.run(["pkill", "-KILL", "-x", "mvr-vm2"], check=True)
    assert wait_for(lambda: re.search(r"^vm2\s+stopped\s", run_lab("status").stdout, re.M), 10)
    assert wait_for(lambda: port("p2")["oper"] == "down", 5)
    before = common.reasons()["port_down"]
    guest("vm1", "ping -c 2 -W 1 10.0.1.12", check=False)
    assert common.reasons()["port_down"] > before
    r = run_lab("up")
    assert r.returncode == 0, r.stderr
    assert re.search(r"^vm2\s+ready\s", run_lab("status").stdout, re.M)
    assert wait_for(lambda: port("p2")["oper"] == "up", 10)
    r = guest("vm1", "ping -c 3 -W 1 10.0.1.12")
    assert " 0% packet loss" in r.stdout, r.stdout
    assert common.forwarder_pid() == forwarder
    time.sleep(0.2)
    assert common.stats()["mbuf"]["in_use"] == 0
