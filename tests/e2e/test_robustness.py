"""US4: bad traffic is attributed, contained and survivable (V13, V14, V17; SC-006, SC-011)."""
import json
import os
import subprocess
import time

import pytest

from conftest import (ROOT, VENV_PY, forwarder_pid, ns_exec, reasons, sh, stats, vrctl)

CORPUS = ROOT / "tests" / "e2e" / "corpus.py"


def ping_bg(ns, *args):
    return subprocess.Popen(["ip", "netns", "exec", ns, "ping", *args], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)


def snmp(ns, proto, field):
    lines = ns_exec(ns, "cat", "/proc/net/snmp").stdout.splitlines()
    for hdr, val in zip(lines, lines[1:]):
        if hdr.startswith(proto + ":") and val.startswith(proto + ":"):
            return int(dict(zip(hdr.split()[1:], val.split()[1:]))[field])
    raise KeyError(field)


def test_v13_corpus_is_fully_attributed_and_forwarding_continues(forwarder):
    pid = forwarder_pid()
    vrctl("stats", "clear")
    pinger = ping_bg("mvr-vm2", "-c", "300", "-i", "0.01", "10.0.2.13")  # unaffected traffic
    r = sh(VENV_PY, CORPUS, "--ns", "mvr-vm1", "--count", "20", timeout=120)
    out = json.loads(r.stdout.strip().splitlines()[-1])
    ping_out, _ = pinger.communicate(timeout=60)
    assert " 0% packet loss" in ping_out, ping_out[-300:]  # SC-006: no collateral damage

    time.sleep(0.5)
    got = {k: v for k, v in reasons().items() if v}
    assert got == out["expected"], f"expected {out['expected']}, got {got}"  # 100% attributed
    d = stats()["drops"]
    assert d["total"] == sum(d["reasons"].values()) == out["sent"]
    assert forwarder_pid() == pid and os.path.exists(f"/proc/{pid}")
    assert stats()["mbuf"]["in_use"] == 0  # back to baseline


def test_v14_icmp_error_flood_is_rate_limited_and_other_ports_still_get_errors(forwarder):
    vrctl("stats", "clear")
    before = snmp("mvr-vm1", "Icmp", "InDestUnreachs")
    # ~500 packets/s of echo requests to an unroutable address for 10 s, from vm1's real MAC/IP
    flood = subprocess.Popen(
        ["ip", "netns", "exec", "mvr-vm1", str(VENV_PY), str(ROOT / "tests" / "e2e" / "pkt_tool.py"),
         "flood", "--dst", "10.9.9.9", "--src-ip", "10.0.1.11", "--src-mac", "02:00:00:00:01:01",
         "--gw-mac", "02:00:00:00:00:01", "--pps", "500", "--seconds", "10"],
        stdout=subprocess.PIPE, text=True)
    time.sleep(3)
    # a different port has its own bucket: vm3 still gets Time Exceeded for every hop probe
    for ttl, who in ((1, "10.0.2.1"), (2, None)):
        r = ns_exec("mvr-vm3", "ping", "-c", "1", "-t", str(ttl), "-W", "1", "10.0.1.11", check=False)
        if who:
            assert f"From {who}" in r.stdout and "Time to live exceeded" in r.stdout, r.stdout
        else:
            assert " 0% packet loss" in r.stdout, r.stdout
    out, _ = flood.communicate(timeout=30)
    sent = json.loads(out.strip().splitlines()[-1])["sent"]
    time.sleep(0.3)
    received = snmp("mvr-vm1", "Icmp", "InDestUnreachs") - before
    s = stats()
    assert sent >= 2000, f"flood too slow: {sent} packets"
    assert received <= 1020, f"vm1 got {received} ICMP errors"  # 100/s x 10 s + burst 20
    assert received >= 800, f"limiter is stricter than 100/s: {received} errors"
    assert s["events"]["icmp_ratelimited"] > 0
    assert s["drops"]["reasons"]["no_route"] >= sent  # every packet still counted as a drop
    assert s["drops"]["total"] == sum(s["drops"]["reasons"].values())


def wait_for(pred, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if pred():
            return True
        time.sleep(0.05)
    return pred()


def oper(port):
    return [p for p in vrctl("port", "show")["data"]["ports"] if p["name"] == port][0]["oper"]


def test_link_flap_follows_the_veth_state_without_restart(forwarder):
    pid = forwarder_pid()
    assert oper("p3") == "up"
    ns_exec("mvr-vm3", "ip", "link", "set", "eth0", "down")
    try:
        assert wait_for(lambda: oper("p3") == "down", 1.0), "p3 should be oper-down within 1 s"
        before = reasons()["port_down"]
        ns_exec("mvr-vm1", "ping", "-c", "2", "-W", "1", "10.0.2.13", check=False)
        assert reasons()["port_down"] > before
    finally:
        ns_exec("mvr-vm3", "ip", "link", "set", "eth0", "up")
        # the kernel drops routes through a downed interface; give the "VM" its gateway back
        ns_exec("mvr-vm3", "ip", "route", "replace", "default", "via", "10.0.2.1", "dev", "eth0")
    assert wait_for(lambda: oper("p3") == "up", 1.0), "p3 should be oper-up within 1 s"
    r = ns_exec("mvr-vm1", "ping", "-c", "3", "-W", "1", "10.0.2.13", check=False)
    assert " 0% packet loss" in r.stdout, r.stdout
    assert forwarder_pid() == pid
    time.sleep(0.2)
    assert stats()["mbuf"]["in_use"] == 0


def test_v17_sigterm_shuts_down_within_two_seconds(forwarder):
    pid = forwarder_pid()
    t0 = time.monotonic()
    os.kill(pid, 15)
    assert wait_for(lambda: not os.path.exists(f"/proc/{pid}"), 2.0)
    assert time.monotonic() - t0 <= 2.0
    log = (ROOT / "build" / "lab" / "minivrd.log").read_text()
    assert 'msg="shutdown complete"' in log.splitlines()[-1] or 'msg="shutdown complete"' in log
