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

A recorded demo (asciinema) will go here: bring-up, ping, live `route del`/`route add`,
the malformed-traffic corpus, and the crash drill. Until then, `docs/lab.md` and
`specs/001-lab-l3-forwarder/quickstart.md` are the click-by-click version.

## Documentation

| | |
|---|---|
| `docs/architecture.md` | event loop, packet path, data structures, mbuf lifecycle |
| `docs/lab.md` | topology, addressing, commands, safety guarantees |
| `docs/cli.md` | every `vrctl` command, exit code, error code and JSON shape |
| `docs/drop-reasons.md` | every named drop reason, its trigger and its test |
| `docs/traceability.md` | each requirement and success criterion mapped to a test |
| `incidents/` | post-mortems, starting with the forced-crash drill |
| `specs/001-lab-l3-forwarder/` | spec, plan, research decisions, data model, contracts, tasks |

## Layout

`src/dp` forwarding logic (no syscalls), `src/pio` packet I/O backends, `src/ctl` command grammar
and control socket, `src/daemon` the `minivrd` main/loop/privileges/crash/link monitor,
`src/vrctl` the CLI, `lab/` lab scripts and shipped configs, `tests/unit` C unit tests,
`tests/e2e` pytest + scapy end-to-end suite. Third-party test dependencies: `THIRD_PARTY.md`.

## Status

Phase 1 (single-node lab and L3 forwarder). Not claimed: performance (no benchmarks yet),
multi-core, tunnelling, IPv6, VLANs, ARP/neighbour aging. See the spec's assumptions.
