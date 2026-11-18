"""US1: lab bring-up/teardown (contracts/lab.md, FR-001..FR-004, SC-002)."""
import json
import time

import pytest

from conftest import host_snapshot, lab_sh, ns_exec, sh

VMS = [
    ("mvr-vm1", "02:00:00:00:01:01", "10.0.1.11/24", "10.0.1.1", "p1", "02:00:00:00:00:01"),
    ("mvr-vm2", "02:00:00:00:01:02", "10.0.1.12/24", "10.0.1.1", "p2", "02:00:00:00:00:02"),
    ("mvr-vm3", "02:00:00:00:01:03", "10.0.2.13/24", "10.0.2.1", "p3", "02:00:00:00:00:03"),
]


def ip_json(ns, *args):
    return json.loads(ns_exec(ns, "ip", "-j", *args).stdout)


def sysctl(ns, path):
    return ns_exec(ns, "cat", f"/proc/sys/{path}").stdout.strip()


def test_topology_matches_contract(lab):
    names = sh("ip", "netns", "list").stdout
    for ns in ("mvr-cn1", "mvr-vm1", "mvr-vm2", "mvr-vm3"):
        assert ns in names

    for ns, mac, addr, gw, port, pmac in VMS:
        eth = ip_json(ns, "addr", "show", "dev", "eth0")[0]
        assert eth["address"] == mac
        assert eth["operstate"] in ("UP", "LOWERLAYERDOWN", "UNKNOWN")
        v4 = [a for a in eth["addr_info"] if a["family"] == "inet"]
        assert [f'{a["local"]}/{a["prefixlen"]}' for a in v4] == [addr]
        routes = ip_json(ns, "route", "show", "default")
        assert len(routes) == 1 and routes[0]["gateway"] == gw and routes[0]["dev"] == "eth0"
        assert sysctl(ns, "net/ipv6/conf/eth0/disable_ipv6") == "1"

        p = ip_json("mvr-cn1", "addr", "show", "dev", port)[0]
        assert p["address"] == pmac
        assert [a for a in p["addr_info"] if a["family"] == "inet"] == []  # FR-004: no IPv4
        assert sysctl("mvr-cn1", f"net/ipv6/conf/{port}/disable_ipv6") == "1"
        assert sysctl("mvr-cn1", f"net/ipv4/conf/{port}/arp_ignore") == "8"

    assert sysctl("mvr-cn1", "net/ipv4/ip_forward") == "0"
    for ns in ("mvr-cn1", "mvr-vm1", "mvr-vm2", "mvr-vm3"):
        lo = ip_json(ns, "addr", "show", "dev", "lo")[0]
        assert "UP" in lo["flags"]


def test_up_is_idempotent(lab):
    before = {ns: ns_exec(ns, "ip", "-j", "addr").stdout for ns, *_ in VMS}
    lab_sh("up")
    lab_sh("up")
    after = {ns: ns_exec(ns, "ip", "-j", "addr").stdout for ns, *_ in VMS}
    assert before == after


def test_down_after_partial_up():
    lab_sh("down", check=False)
    before = host_snapshot()
    # simulate an interrupted bring-up: some namespaces, a veth still in the root namespace
    sh("ip", "netns", "add", "mvr-cn1")
    sh("ip", "netns", "add", "mvr-vm1")
    sh("ip", "link", "add", "mvr-vm1-e0", "type", "veth", "peer", "name", "mvr-p1")
    lab_sh("down")
    assert host_snapshot() == before
    lab_sh("down")  # second down is a no-op, not an error


def test_up_recovers_from_partial_state():
    lab_sh("down", check=False)
    sh("ip", "netns", "add", "mvr-vm2")
    sh("ip", "link", "add", "mvr-vm2-e0", "type", "veth", "peer", "name", "mvr-p2")
    lab_sh("up")
    assert ip_json("mvr-vm2", "addr", "show", "dev", "eth0")[0]["address"] == "02:00:00:00:01:02"
    lab_sh("down")


def test_timing_budget():
    lab_sh("down", check=False)
    t0 = time.monotonic()
    lab_sh("up")
    up = time.monotonic() - t0
    t0 = time.monotonic()
    lab_sh("down")
    down = time.monotonic() - t0
    assert up < 10, f"bring-up took {up:.1f}s"
    assert down < 5, f"teardown took {down:.1f}s"


def ens33_state():
    """ens33 addresses without DHCP lease timers, which count down by themselves."""
    r = sh("ip", "-j", "addr", "show", "ens33", check=False)
    if r.returncode != 0:
        return None
    links = json.loads(r.stdout)
    for link in links:
        for a in link.get("addr_info", []):
            a.pop("valid_life_time", None)
            a.pop("preferred_life_time", None)
    return links


def test_host_state_unchanged_and_ens33_untouched():
    lab_sh("down", check=False)
    before = host_snapshot()
    ens = ens33_state()
    lab_sh("up")
    lab_sh("down")
    assert host_snapshot() == before
    assert ens33_state() == ens


def test_scripts_never_reference_the_host_nic():
    from conftest import ROOT
    for f in (ROOT / "lab").glob("*"):
        if f.is_file():
            assert "ens33" not in f.read_text(), f"{f} references the host NIC"


def test_down_removes_every_lab_object():
    lab_sh("up")
    lab_sh("down")
    assert "mvr-" not in sh("ip", "netns", "list").stdout
    assert "mvr-" not in sh("ip", "-br", "link").stdout
    from pathlib import Path
    assert not Path("/run/mvr-cn1").exists()
