"""US2: VMs reach each other through minivrd (quickstart V1-V8, V12, V16; SC-003, SC-004)."""
import concurrent.futures
import hashlib
import os
import random
import re
import subprocess
import time

import pytest

from conftest import (ROOT, forwarder_pid, ns_exec, pkt_tool, pkt_tool_bg, reasons, sh,
                      start_forwarder, vrctl)

VMS = {"mvr-vm1": "10.0.1.11", "mvr-vm2": "10.0.1.12", "mvr-vm3": "10.0.2.13"}
P1_MAC = "02:00:00:00:00:01"


def ping(ns, *args, timeout=60):
    return ns_exec(ns, "ping", *args, check=False, timeout=timeout)


def test_v1_arp_for_the_gateway(forwarder):
    r = pkt_tool("mvr-vm1", "arp-who", "--ip", "10.0.1.1", "--src", "10.0.1.11")
    assert [x["mac"] for x in r["replies"]] == [P1_MAC]


def test_v2_proxy_arp_for_another_vm_without_touching_that_vm(forwarder):
    sniffer = pkt_tool_bg("mvr-vm2", "sniff", "--bpf", "arp", "--seconds", "2")
    r = pkt_tool("mvr-vm1", "arp-who", "--ip", "10.0.1.12", "--src", "10.0.1.11")
    assert [x["mac"] for x in r["replies"]] == [P1_MAC]  # the forwarder answers, not vm2
    out, _ = sniffer.communicate(timeout=15)
    assert '"count": 0' in out, f"vm2 saw ARP traffic: {out}"


def test_v3_arp_for_an_unknown_address_is_not_answered(forwarder):
    before = reasons()["arp_unknown_target"]
    r = pkt_tool("mvr-vm1", "arp-who", "--ip", "10.0.1.99", "--src", "10.0.1.11", "--count", "2")
    assert r["replies"] == []
    assert reasons()["arp_unknown_target"] - before == 2


def test_v4_every_ordered_pair_pings_with_no_loss_and_ttl_63(forwarder):
    pairs = [(a, b) for a in VMS for b in VMS if a != b]
    assert len(pairs) == 6

    def run(pair):
        a, b = pair
        return pair, ping(a, "-c", "1000", "-i", "0.01", "-W", "1", VMS[b], timeout=120)

    with concurrent.futures.ThreadPoolExecutor(6) as ex:
        results = list(ex.map(run, pairs))
    for (a, b), r in results:
        assert " 0% packet loss" in r.stdout, f"{a}->{b}: {r.stdout[-300:]}"
        ttls = set(re.findall(r"ttl=(\d+)", r.stdout))
        assert ttls == {"63"}, f"{a}->{b}: ttl values {ttls}"


def test_v5_unknown_network_is_unreachable(forwarder):
    r = ping("mvr-vm1", "-c", "1", "-W", "1", "10.9.9.9")
    assert "Destination Net Unreachable" in r.stdout
    assert "From 10.0.1.1" in r.stdout
    assert reasons()["no_route"] >= 1


def test_v6_ttl_exceeded(forwarder):
    r = ping("mvr-vm1", "-c", "1", "-t", "1", "-W", "1", "10.0.2.13")
    assert "Time to live exceeded" in r.stdout
    assert reasons()["ttl_exceeded"] >= 1


def test_v7_gateway_answers_echo(forwarder):
    r = ping("mvr-vm1", "-c", "3", "-W", "1", "10.0.1.1")
    assert " 0% packet loss" in r.stdout
    assert "ttl=64" in r.stdout


def test_v8_one_gigabyte_transfer_keeps_its_checksum(forwarder):
    """Streams the data (nothing is written to disk: /tmp may be a small tmpfs)."""
    mb = int(os.environ.get("MINIVR_E2E_XFER_MB", "1024"))
    rng = random.Random(1234)  # /dev/urandom is far too slow for 1 GiB on some hosts
    receiver = subprocess.Popen(
        ["ip", "netns", "exec", "mvr-vm3", "sh", "-c",
         "socat -u TCP-LISTEN:9000,reuseaddr STDOUT | sha256sum"],
        stdout=subprocess.PIPE, text=True)
    sender = None
    try:
        time.sleep(0.5)
        sender = subprocess.Popen(
            ["ip", "netns", "exec", "mvr-vm1", "socat", "-u", "STDIN",
             "TCP:10.0.2.13:9000,retry=10,interval=0.2"], stdin=subprocess.PIPE)
        digest = hashlib.sha256()
        t0 = time.monotonic()
        for _ in range(mb):
            chunk = rng.randbytes(1024 * 1024)
            digest.update(chunk)
            sender.stdin.write(chunk)
        sender.stdin.close()
        assert sender.wait(timeout=300) == 0
        out, _ = receiver.communicate(timeout=120)
        elapsed = time.monotonic() - t0
    finally:
        for proc in (sender, receiver):
            if proc and proc.poll() is None:
                proc.kill()
    assert out.split()[0] == digest.hexdigest()
    print(f"transferred {mb} MiB in {elapsed:.1f}s")
    assert vrctl("stats", "show")["data"]["mbuf"]["in_use"] == 0


def test_v12_spoofed_source_address_is_dropped(forwarder):
    ns_exec("mvr-vm1", "ip", "addr", "add", "10.0.1.12/32", "dev", "eth0")
    try:
        before = reasons()["source_spoofed"]
        r = ping("mvr-vm1", "-I", "10.0.1.12", "-c", "2", "-W", "1", "10.0.2.13")
        assert " 100% packet loss" in r.stdout
        assert reasons()["source_spoofed"] > before
    finally:
        ns_exec("mvr-vm1", "ip", "addr", "del", "10.0.1.12/32", "dev", "eth0", check=False)


def test_v16_unbound_vm_is_resolved_learned_and_the_first_ping_works(lab):
    pid = start_forwarder("cn1-nobind.conf")
    assert pid
    r = ping("mvr-vm1", "-c", "1", "-W", "2", "10.0.2.13")
    assert " 0% packet loss" in r.stdout, r.stdout
    neigh = {n["ip"]: n for n in vrctl("neigh", "show")["data"]["neighbours"]}
    assert neigh["10.0.2.13"]["kind"] == "learned"
    assert neigh["10.0.2.13"]["state"] == "REACHABLE"
    assert neigh["10.0.2.13"]["mac"] == "02:00:00:00:01:03"
    assert forwarder_pid() == pid
