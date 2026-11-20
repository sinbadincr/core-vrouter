"""US4 (spec 004): phase 1 behaviours a guest can show, proven from inside the KVM guests (R3).

V3 unknown address not answered, V7 gateway answers echo, V14 ICMP error rate limit (V10 and
V16 need the unbound config and live in test_kvm_unbound.py). The matching harness tests are in tests/e2e; docs/traceability.md maps each pair.
"""
import time
from concurrent.futures import ThreadPoolExecutor

import pytest

from conftest import common, guest

pytestmark = pytest.mark.usefixtures("forwarder")


def test_v3_unknown_address_arp_not_answered():
    r = guest("vm1", "sudo arping -c 2 -w 3 -I eth0 10.0.1.99", check=False)
    assert r.returncode != 0, r.stdout
    assert "02:00:00:00:00:01" not in r.stdout.lower(), r.stdout


def test_v7_gateway_answers_echo():
    r = guest("vm1", "ping -c 3 -W 1 10.0.1.1")
    assert " 0% packet loss" in r.stdout, r.stdout
    assert "ttl=64" in r.stdout, r.stdout


def test_v14_icmp_errors_rate_limited():
    """Parallel fast pings to an unroutable address from one guest port: replies capped at the
    configured rate (100/s, burst 20) and counted. A guest that cannot send fast enough to reach
    the limit skips with its measured rate (recorded in docs/traceability.md)."""
    common.vrctl("stats", "clear")
    t0 = time.monotonic()

    def flood(_):
        return guest("vm1", "ping -c 1500 -i 0.01 -W 1 10.9.9.9", timeout=120, check=False)

    with ThreadPoolExecutor(4) as ex:
        list(ex.map(flood, range(4)))
    elapsed = time.monotonic() - t0
    time.sleep(0.3)
    s = common.stats()
    sent = s["drops"]["reasons"]["no_route"]
    assert s["drops"]["total"] == sum(s["drops"]["reasons"].values())
    if sent < 100 * elapsed + 100:
        pytest.skip(f"guests sent {sent / elapsed:.0f} pkt/s, below the 100/s limit")
    assert s["events"]["icmp_ratelimited"] > 0
    # a different port has its own bucket: vm3 still gets Time Exceeded
    r = guest("vm3", "ping -c 1 -t 1 -W 1 10.0.1.11", check=False)
    assert "Time to live exceeded" in r.stdout, r.stdout
