"""Malformed-traffic corpus (US4, quickstart V13, SC-006).

    sudo .venv/bin/python tests/e2e/corpus.py --ns mvr-vm1 [--count N]

Sends N copies of every malformed class from vm1 towards the forwarder and prints the expected
{reason: count} as JSON. Frames are crafted so that the *first* failing check is the named one
(the checks run in the order documented in docs/drop-reasons.md), and they come from vm1's real
MAC and IP so the anti-spoofing check passes unless spoofing is the point.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from collections import Counter

P1_MAC = "02:00:00:00:00:01"
P2_MAC = "02:00:00:00:00:02"
VM1_MAC = "02:00:00:00:01:01"
VM1_IP = "10.0.1.11"
VM3_IP = "10.0.2.13"


def ip_header(src=VM1_IP, dst=VM3_IP, ttl=64, proto=17, total=None, ihl=5, version=4, flags=0,
              frag=0, payload_len=8, bad_csum=False, options=b""):
    """Raw IPv4 header bytes with a valid checksum unless told otherwise."""
    import socket
    import struct
    hdr_len = ihl * 4
    if total is None:
        total = max(hdr_len, 20 + len(options)) + payload_len
    vihl = (version << 4) | ihl
    ff = (flags << 13) | frag
    h = struct.pack("!BBHHHBBH4s4s", vihl, 0, total, 0x1234, ff, ttl, proto, 0,
                    socket.inet_aton(src), socket.inet_aton(dst)) + options
    s = 0
    for i in range(0, 20, 2):
        s += (h[i] << 8) | h[i + 1]
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    csum = (~s) & 0xFFFF
    if bad_csum:
        csum ^= 0x00FF
    return h[:10] + struct.pack("!H", csum) + h[12:]


def eth(dst, etype, src=VM1_MAC):
    from scapy.all import Ether
    return bytes(Ether(dst=dst, src=src, type=etype))


def classes(dst_mac=P1_MAC):
    """(name, expected reason, frame bytes)."""
    pad = b"\x00" * 8
    out = []
    out.append(("truncated_ip", "truncated", eth(dst_mac, 0x0800) + b"\x45\x00\x00\x1c\x00\x00"))
    out.append(("bad_version", "bad_ip_version", eth(dst_mac, 0x0800) + ip_header(version=6) + pad))
    out.append(("ihl_lt_5", "bad_ip_hdr_len", eth(dst_mac, 0x0800) + ip_header(ihl=4) + pad))
    out.append(("ihl_beyond_frame", "bad_ip_hdr_len",
                eth(dst_mac, 0x0800) + ip_header(ihl=15)[:20] + b"\x00" * 4))
    out.append(("ip_options", "ip_options",
                eth(dst_mac, 0x0800) + ip_header(ihl=6, options=b"\x01" * 4) + pad))
    out.append(("total_len_gt_frame", "bad_ip_total_len",
                eth(dst_mac, 0x0800) + ip_header(total=200) + pad))
    out.append(("total_len_lt_header", "bad_ip_total_len",
                eth(dst_mac, 0x0800) + ip_header(total=10) + pad))
    out.append(("bad_checksum", "bad_ip_checksum",
                eth(dst_mac, 0x0800) + ip_header(bad_csum=True) + pad))
    out.append(("more_fragments", "ip_fragment", eth(dst_mac, 0x0800) + ip_header(flags=1) + pad))
    out.append(("fragment_offset", "ip_fragment", eth(dst_mac, 0x0800) + ip_header(frag=5) + pad))
    out.append(("ipv6", "unsupported_ethertype", eth(dst_mac, 0x86DD) + b"\x60" + b"\x00" * 47))
    out.append(("vlan", "unsupported_ethertype", eth(dst_mac, 0x8100) + b"\x00\x05\x08\x00" + b"\x00" * 30))
    out.append(("broadcast_non_arp", "non_arp_bcast_mcast",
                eth("ff:ff:ff:ff:ff:ff", 0x0800) + ip_header(dst="10.0.1.255") + pad))
    out.append(("multicast_non_arp", "non_arp_bcast_mcast",
                eth("01:00:5e:00:00:01", 0x0800) + ip_header(dst="224.0.0.1") + pad))
    out.append(("wrong_dst_mac", "not_for_us_l2", eth(P2_MAC, 0x0800) + ip_header() + pad))

    def arp(htype=1, ptype=0x0800, hlen=6, plen=4, op=1, spa=VM1_IP, tpa="10.0.1.1"):
        """ARP request as raw bytes: only the named field is wrong, the sender fields stay at
        their standard offsets so the anti-spoofing check passes and the ARP parser decides."""
        import socket
        import struct
        body = struct.pack("!HHBBH6s4s6s4s", htype, ptype, hlen, plen, op,
                           bytes.fromhex(VM1_MAC.replace(":", "")), socket.inet_aton(spa),
                           b"\x00" * 6, socket.inet_aton(tpa))
        return eth("ff:ff:ff:ff:ff:ff", 0x0806) + body

    out.append(("arp_bad_htype", "bad_arp", arp(htype=6)))
    out.append(("arp_bad_ptype", "bad_arp", arp(ptype=0x86DD)))
    out.append(("arp_bad_hlen", "bad_arp", arp(hlen=8)))
    out.append(("arp_bad_plen", "bad_arp", arp(plen=16)))
    out.append(("arp_bad_op", "bad_arp", arp(op=9)))
    out.append(("arp_unknown_target", "arp_unknown_target", arp(tpa="10.0.1.99")))
    out.append(("spoofed_ip", "source_spoofed",
                eth(dst_mac, 0x0800) + ip_header(src="10.0.1.12") + pad))
    out.append(("spoofed_gateway_ip", "source_spoofed",
                eth(dst_mac, 0x0800) + ip_header(src="10.0.1.1") + pad))
    out.append(("spoofed_mac", "source_spoofed",
                eth(dst_mac, 0x0800, src="02:de:ad:00:00:01") + ip_header() + pad))
    out.append(("spoofed_arp_ip", "source_spoofed", arp(spa="10.0.1.12")))
    return out


def run(iface, count):
    from scapy.all import conf, sendp
    conf.verb = 0
    expected = Counter()
    frames = classes()
    for _name, reason, frame in frames:
        expected[reason] += count
    for _ in range(count):
        for _name, _reason, frame in frames:
            sendp(frame, iface=iface, verbose=0)
        time.sleep(0.001)
    return dict(expected), len(frames) * count


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ns", help="run inside this network namespace (default: current)")
    ap.add_argument("--iface", default="eth0")
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--inner", action="store_true", help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.ns and not a.inner:
        os.execvp("ip", ["ip", "netns", "exec", a.ns, sys.executable, os.path.abspath(__file__),
                         "--inner", "--iface", a.iface, "--count", str(a.count)])
    expected, sent = run(a.iface, a.count)
    print(json.dumps({"expected": expected, "sent": sent}))


if __name__ == "__main__":
    main()
