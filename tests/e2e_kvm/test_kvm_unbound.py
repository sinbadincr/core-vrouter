"""US4 (spec 004): V10 and V16 from inside the guests, with the forwarder started from cn1-nobind.conf.

Its own module: the KVM `forwarder` fixture is module-scoped and other modules compare its pid,
so restarting the forwarder inside a shared module would leave them with a stale pid.
"""
import time

import pytest

from conftest import common, guest


@pytest.fixture(scope="module")
def unbound_forwarder(kvm_lab):
    pid = common.start_forwarder("cn1-nobind.conf")
    assert pid
    yield pid
    common.start_forwarder("cn1.conf")


def test_v16_unbound_neighbour_resolved_learned_first_ping_works(unbound_forwarder):
    r = guest("vm1", "ping -c 1 -W 2 10.0.2.13", check=False)
    assert " 0% packet loss" in r.stdout, r.stdout
    neigh = {n["ip"]: n for n in common.vrctl("neigh", "show")["data"]["neighbours"]}
    assert neigh["10.0.2.13"]["kind"] == "learned"
    assert neigh["10.0.2.13"]["state"] == "REACHABLE"
    assert neigh["10.0.2.13"]["mac"] == "02:00:00:00:01:03"
    assert common.forwarder_pid() == unbound_forwarder


def test_v10_live_route_change_without_restart(unbound_forwarder):
    # cn1-nobind.conf: with p3 bound, replies sourced from 10.0.9.1 are source_spoofed drops
    # (same as the harness test). Never 10.0.2.0/24: connected routes are immutable (tests/e2e/test_cli.py::test_v9)
    guest("vm3", "ip addr add 10.0.9.1/32 dev lo", sudo=True)
    try:
        assert common.vrctl("route", "add", "10.0.9.0/24", "dev", "p3", "via", "10.0.2.13")["ok"]
        r = guest("vm1", "ping -c 2 -W 2 10.0.9.1", check=False)
        assert " 0% packet loss" in r.stdout, r.stdout

        t0 = time.monotonic()
        assert common.vrctl("route", "del", "10.0.9.0/24")["ok"]
        r = guest("vm1", "ping -c 1 -W 1 10.0.9.1", check=False)
        assert "Destination Net Unreachable" in r.stdout, r.stdout
        assert time.monotonic() - t0 < 2.0  # 1 s of ping plus the ssh round trip

        t0 = time.monotonic()
        assert common.vrctl("route", "add", "10.0.9.0/24", "dev", "p3", "via", "10.0.2.13")["ok"]
        r = guest("vm1", "ping -c 1 -W 1 10.0.9.1", check=False)
        assert " 0% packet loss" in r.stdout, r.stdout
        assert time.monotonic() - t0 < 2.0
        assert common.forwarder_pid() == unbound_forwarder  # never restarted
    finally:
        common.vrctl("route", "del", "10.0.9.0/24")
        guest("vm3", "ip addr del 10.0.9.1/32 dev lo", sudo=True, check=False)
