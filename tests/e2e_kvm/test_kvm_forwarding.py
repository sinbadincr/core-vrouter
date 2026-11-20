"""US2: phase 1 forwarding behaviour proven from inside real KVM guests (FR-010, SC-005)."""
import hashlib
import itertools
import re
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor

import pytest

from conftest import LOGS, ROOT, common, guest

pytestmark = pytest.mark.usefixtures("forwarder")

ADDR = {"vm1": "10.0.1.11", "vm2": "10.0.1.12", "vm3": "10.0.2.13"}


def delta(before, after, reason):
    return after[reason] - before[reason]


def test_k4_gateway_and_proxy_arp():
    r = guest("vm1", "sudo arping -c 1 -I eth0 10.0.1.1")
    assert "02:00:00:00:00:01" in r.stdout.lower()
    r = guest("vm1", "sudo arping -c 1 -I eth0 10.0.1.12")  # proxy ARP: answered by the gateway
    assert "02:00:00:00:00:01" in r.stdout.lower()


def test_k5_all_pairs_zero_loss_ttl_63():
    pairs = list(itertools.permutations(ADDR, 2))
    assert len(pairs) == 6

    def run(pair):
        src, dst = pair
        return pair, guest(src, f"sudo ping -c 1000 -i 0.01 -W 1 {ADDR[dst]}", timeout=120)

    with ThreadPoolExecutor(6) as ex:
        for pair, r in ex.map(run, pairs):
            assert " 0% packet loss" in r.stdout, (pair, r.stdout)
            assert "ttl=63" in r.stdout, (pair, r.stdout)


def test_k6_net_unreachable_counted():
    before = common.reasons()
    r = guest("vm1", "ping -c 1 -W 2 10.9.9.9", check=False)
    assert "Destination Net Unreachable" in r.stdout and "10.0.1.1" in r.stdout, r.stdout
    assert delta(before, common.reasons(), "no_route") >= 1


def test_k7_ttl_exceeded():
    r = guest("vm1", "ping -c 1 -t 1 -W 2 10.0.2.13", check=False)
    assert "Time to live exceeded" in r.stdout, r.stdout


def test_k9_spoofed_source_dropped_and_counted():
    before = common.reasons()
    guest("vm1", "ip addr add 10.0.1.12/32 dev eth0", sudo=True)
    try:
        r = guest("vm1", "ping -c 3 -W 1 -I 10.0.1.12 10.0.2.13", check=False)
        assert " 0 received" in r.stdout or "100% packet loss" in r.stdout, r.stdout
    finally:
        guest("vm1", "ip addr del 10.0.1.12/32 dev eth0", sudo=True, check=False)
    assert delta(before, common.reasons(), "source_spoofed") >= 3


def test_k8_100mb_tcp_digest_matches():
    guest("vm3", "rm -f /tmp/recv.sha; nohup sh -c 'nc -l -p 5001 | sha256sum > /tmp/recv.sha' "
                 ">/dev/null 2>&1 &")
    time.sleep(1)
    guest("vm1", "rm -f /tmp/f /tmp/s; mkfifo /tmp/f; (sha256sum </tmp/f >/tmp/s &); "
                 "dd if=/dev/urandom bs=1M count=100 2>/dev/null | tee /tmp/f | nc 10.0.2.13 5001; "
                 "sleep 1", timeout=240)
    sent = guest("vm1", "cat /tmp/s").stdout.split()[0]
    recv = ""
    for _ in range(30):
        recv = guest("vm3", "cat /tmp/recv.sha 2>/dev/null", check=False).stdout.strip()
        if recv:
            break
        time.sleep(1)
    assert len(sent) == 64 and recv.split()[0] == sent
    time.sleep(0.2)
    assert common.stats()["mbuf"]["in_use"] == 0


def test_k10_same_binary_and_config_as_harness():
    pid = common.forwarder_pid()
    exe = hashlib.sha256(open(f"/proc/{pid}/exe", "rb").read()).hexdigest()
    built = hashlib.sha256((ROOT / "build" / common.BUILD / "minivrd").read_bytes()).hexdigest()
    assert exe == built
    argv = open(f"/proc/{pid}/cmdline", "rb").read().split(b"\0")
    assert argv[argv.index(b"--config") + 1].decode() == str(ROOT / "lab" / "cn1.conf")
    r = subprocess.run(["git", "-c", "safe.directory=*", "diff", "--quiet", "HEAD", "--",
                        "lab/cn1.conf"], cwd=ROOT)
    assert r.returncode == 0, "lab/cn1.conf differs from git"
    log = (ROOT / "build" / "lab" / "minivrd.log").read_text()
    ident = [l for l in log.splitlines() if l.startswith("identity ")][-1]
    cfg = hashlib.sha256((ROOT / "lab" / "cn1.conf").read_bytes()).hexdigest()
    assert f"minivrd={built}" in ident and f"config={cfg}" in ident and "mode=kvm" in ident
