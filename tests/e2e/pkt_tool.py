"""Scapy helper run inside a lab namespace: `ip netns exec <ns> python pkt_tool.py <cmd> ...`.
Prints one JSON line (after an optional READY line for background commands)."""
import argparse
import json
import sys
import time

from scapy.all import ARP, AsyncSniffer, Ether, conf, srp

conf.verb = 0


def cmd_arp_who(a):
    replies = []
    for _ in range(a.count):
        ans, _ = srp(Ether(dst="ff:ff:ff:ff:ff:ff") / ARP(op=1, pdst=a.ip, psrc=a.src),
                     iface=a.iface, timeout=a.timeout, verbose=0)
        replies += [{"ip": r[1].psrc, "mac": r[1].hwsrc} for r in ans]
    print(json.dumps({"replies": replies}))


def cmd_sniff(a):
    sn = AsyncSniffer(iface=a.iface, filter=a.bpf)
    sn.start()
    time.sleep(0.5)
    print("READY", flush=True)
    time.sleep(a.seconds)
    sn.stop()
    pkts = sn.results or []
    print(json.dumps({"count": len(pkts), "summaries": [p.summary() for p in pkts][:10]}))


def cmd_flood(a):
    """Paced stream of ICMP echo requests to an unroutable address, from the VM's real MAC/IP."""
    import socket
    from scapy.all import ICMP, IP
    frame = bytes(Ether(dst=a.gw_mac, src=a.src_mac) / IP(src=a.src_ip, dst=a.dst) / ICMP() / (b"x" * 16))
    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
    sock.bind((a.iface, 0))
    interval = 1.0 / a.pps
    start = time.monotonic()
    sent = 0
    while time.monotonic() - start < a.seconds:
        sock.send(frame)
        sent += 1
        due = start + sent * interval
        delay = due - time.monotonic()
        if delay > 0:
            time.sleep(delay)
    print(json.dumps({"sent": sent}))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("arp-who")
    p.add_argument("--iface", default="eth0")
    p.add_argument("--ip", required=True)
    p.add_argument("--src", required=True)
    p.add_argument("--count", type=int, default=1)
    p.add_argument("--timeout", type=float, default=1.0)
    p.set_defaults(fn=cmd_arp_who)
    p = sub.add_parser("sniff")
    p.add_argument("--iface", default="eth0")
    p.add_argument("--bpf", default="")
    p.add_argument("--seconds", type=float, default=3.0)
    p.set_defaults(fn=cmd_sniff)
    p = sub.add_parser("flood")
    p.add_argument("--iface", default="eth0")
    p.add_argument("--dst", required=True)
    p.add_argument("--src-ip", required=True)
    p.add_argument("--src-mac", required=True)
    p.add_argument("--gw-mac", required=True)
    p.add_argument("--pps", type=int, default=500)
    p.add_argument("--seconds", type=float, default=10.0)
    p.set_defaults(fn=cmd_flood)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
