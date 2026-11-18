# minivr

A learning/portfolio **vRouter data plane** in C17 for Linux, built the way a production
forwarder would be: a single-threaded L3 forwarder with a pluggable packet I/O backend, a live
control CLI, structured logs, crash dumps, sanitizer-clean unit tests and a one-command
end-to-end suite on a reproducible network-namespace lab.

It models the data-plane half of Tungsten Fabric's vRouter. The compute node is a namespace
(`mvr-cn1`) running `minivrd`; the "VMs" are namespaces joined to it by veth pairs; the
forwarder presents an **anycast gateway** to every VM, answers ARP only for hosts it knows
(proxy ARP), drops spoofed sources, routes IPv4 by longest-prefix match and counts every drop
under a named reason.

```
 mvr-vm1 ─┐                                  ┌─ mvr-vm3
 10.0.1.11├── p1 ┐                      ┌ p3 ┤ 10.0.2.13
 mvr-vm2 ─┤     ├── minivrd (AF_PACKET) ┤    └─ gw 10.0.2.1
 10.0.1.12└── p2 ┘   mvr-cn1             └
   gw 10.0.1.1 (anycast on p1 and p2)
```

## Quickstart

```bash
sudo apt install build-essential cmake clang gdb iproute2 iputils-ping \
                 socat jq python3-venv             # arping/tcpdump/iperf3 help for manual poking
cmake --preset debug && cmake --build --preset debug
ctest --preset debug                               # unit tests (also: --preset asan)

sudo make lab && sudo make lab-run                 # namespaces + minivrd with lab/cn1.conf
sudo ip netns exec mvr-vm1 ping -c3 10.0.2.13      # vm1 -> vm3 through the forwarder, ttl=63
export MINIVR_SOCK=/run/mvr-cn1/minivr.sock
sudo -E build/debug/vrctl route show
sudo -E build/debug/vrctl -j stats show | jq .data.drops
sudo make lab-clean                                # leaves the host network exactly as it was
```

`sudo make e2e` runs everything (unit-level lab checks, forwarding, CLI, robustness corpus,
privilege drop, crash drill) and compares the host network state before and after.
`make check` is the merge gate: GCC and Clang with `-Werror`, ctest plain and under ASan/UBSan,
and the fast-path audit.

## Demo

`sudo scripts/demo.sh` walks through the whole thing: lab up, forwarder start, pings between VMs,
the route/neighbour/port tables, a live `route del`/`route add` while pinging, the malformed
traffic corpus landing in named drop reasons, and teardown. A captured run is in
[`docs/demo.md`](docs/demo.md); `sudo asciinema rec -c scripts/demo.sh docs/demo.cast` records a
terminal cast of it.

## Benchmarks

None in phase 1, on purpose: phase 1 optimises for correctness and observability. Any
optimisation from phase 4 on needs a committed benchmark, perf evidence and an entry in
`docs/perf.md` (constitution VI), and numbers will be labelled as VM/virtual-device results,
not line rate.

## Documentation

| | |
|---|---|
| `docs/architecture.md` | event loop, packet path, data structures, mbuf lifecycle |
| `docs/lab.md` | topology, addressing, commands, safety guarantees |
| `docs/cli.md` | every `vrctl` command, exit code, error code and JSON shape |
| `docs/drop-reasons.md` | every named drop reason, its trigger and its test |
| `docs/demo.md` | captured output of the scripted demo |
| `docs/traceability.md` | each requirement and success criterion mapped to a test |
| `incidents/` | post-mortems, starting with the forced-crash drill |
| `specs/001-lab-l3-forwarder/` | spec, plan, research decisions, data model, contracts, tasks |

## Layout

`src/dp` forwarding logic (no syscalls), `src/pio` packet I/O backends, `src/ctl` command grammar
and control socket, `src/daemon` the `minivrd` main/loop/privileges/crash/link monitor,
`src/vrctl` the CLI, `lab/` lab scripts and shipped configs, `tests/unit` C unit tests,
`tests/e2e` pytest + scapy end-to-end suite. Third-party test dependencies: `THIRD_PARTY.md`.

## Roadmap

| Phase | Scope | Status |
|---|---|---|
| 1. Lab + L3 forwarder | Namespace lab (`mvr-cn1` + 3 VMs); AF_PACKET/epoll forwarder with proxy ARP, anycast gateway, LPM trie, neighbour FSM, ICMP rate limit, anti-spoofing; `vrctl` CLI; privilege drop; crash dumps and post-mortem | **Done** |
| 1.5. KVM lab mode | Optional lab mode replacing namespace VMs with real KVM guests (CirrOS, virtio-net) attached to `minivrd` through TAP ports, as nova/libvirt attach VMs to a vRouter; no forwarder changes | Planned |
| 2. VRFs + overlay tunnels | Two compute nodes over an emulated underlay; tenants with overlapping addresses; MPLSoUDP (RFC 7510) and VXLAN (RFC 7348); interop with the Linux kernel VXLAN | Spec draft |
| 3. Agent + controller | Controller holding network state; per-node agent programming `minivrd` over a documented protocol, replacing static remote routes | Planned |
| 4. Multi-core + DPDK + perf | Worker threads with RCU-updated tables, DIR-24-8 LPM; optional DPDK backend on virtual devices and vhost-user for KVM guests; benchmarks, perf and flame graphs in `docs/perf.md` | Planned |
| 5. BGP EVPN + incidents | EVPN peering with FRR (type-5/type-2 routes); post-mortems of deliberately reproduced faults (races, use-after-free, deadlocks) | Planned |

## Status

Phase 1 (single-node lab and L3 forwarder). Not claimed: performance (see Benchmarks),
multi-core, tunnelling, IPv6, VLANs, ARP/neighbour aging. See the spec's assumptions.
