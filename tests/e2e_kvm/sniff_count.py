#!/usr/bin/env python3
"""Count frames matching a BPF filter on one interface until a line arrives on stdin.

Usage (inside the namespace): sniff_count.py IFACE BPF   -> prints READY, then {"frames": N}
"""
import json
import sys

from scapy.all import AsyncSniffer, conf

conf.verb = 0
iface, bpf = sys.argv[1], sys.argv[2]
sn = AsyncSniffer(iface=iface, filter=bpf, store=True)
sn.start()
print("READY", flush=True)
sys.stdin.readline()
res = sn.stop()
print(json.dumps({"frames": len(res)}), flush=True)
