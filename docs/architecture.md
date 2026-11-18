# minivr architecture (phase 1)

`minivrd` is a single-threaded, run-to-completion L3 forwarder. `vrctl` is its CLI. Everything
below describes what is built, not what is planned; see `specs/001-lab-l3-forwarder/` for the
requirements and decisions (R1 to R17).

## Processes and libraries

```
 vrctl ──(Unix socket, length-prefixed, JSON replies)──► minivrd
                                                          │
 libminivr_dp  = src/common + src/dp + src/ctl/{cmd,frame}.c   (linked by minivrd, vrctl, unit tests)
 minivrd only  = src/daemon + src/pio/pio_afpacket.c + src/ctl/{config,ctl_server}.c
```

`src/dp` makes **no syscalls**: no allocation, locks, blocking calls, I/O or unthrottled logging
(`scripts/check-fastpath.sh` enforces it in `make check`). All packet I/O goes through the
`pio_ops` table in `src/pio/pio.h`; time comes from `src/common/clock.h`. That is what lets
`tests/unit` run the whole forwarding pipeline against `pio_fake` and a fake clock, and it is the
seam a DPDK backend will use later.

## Event loop and file descriptors

One `epoll` loop (`src/daemon/evloop.c`) owns every fd, each wrapped in an `evsrc` with a callback:

| fd | Callback | Does |
|---|---|---|
| one AF_PACKET socket per port | `on_port` | `rx_burst` up to 32 frames, `forward_burst` |
| `signalfd` (SIGINT, SIGTERM, SIGHUP) | `on_signal` | TERM/INT: leave the loop; HUP: ignored with a log line |
| `timerfd`, 10 ms | `on_tick` | `neigh_tick` (ARP retries, resolution deadlines), idle control clients |
| rtnetlink `RTMGRP_LINK` | `linkmon` | `IFF_RUNNING` changes become `port.oper_up`; down releases held packets |
| control listener + up to 8 clients | `ctl_server` | frame, `SO_PEERCRED` check, `cmd_exec`, reply |

Control commands run on the same thread **between** packet bursts, so a route change is visible to
the very next packet and no data structure needs a lock.

Start-up order is fixed: parse config (opens the port sockets) -> rtnetlink -> control socket ->
create `/var/tmp/minivr` -> drop to `nobody` with zero capabilities and `no_new_privs`, then
verify it from `/proc/self/status` -> loop. A failure at any step exits non-zero before a single
packet is processed.

## Packet path (`src/dp/forward.c`)

```
epoll(port fd) -> pio.rx_burst(<=32 mbufs)
 for each mbuf:
   Ethernet classify ─ truncated / bcast-mcast non-ARP / not our MAC / not IPv4+ARP ──► drop(reason)
   kernel-stripped VLAN tag (PACKET_AUXDATA) ───────────────────────────────────────► drop(unsupported_ethertype)
   anti-spoof, if the port is bound to a VM ────────────────────────────────────────► drop(source_spoofed)
   ARP: validate; learn (unbound ports only); reply for own IP / known neighbour; else drop
   IPv4: validate (version, IHL, options, length, checksum, fragments) ─────────────► drop(reason)
         own address? echo request -> reply, else drop(local_unsupported)
         TTL <= 1 -> ICMP time exceeded (rate-limited) + drop(ttl_exceeded)
         LPM -> miss -> net unreachable;  multi-port subnet without /32 -> host unreachable
         egress down -> drop(port_down);  > MTU -> frag-needed if DF, drop(mtu_exceeded)
         TTL--, incremental checksum (RFC 1624)
         neighbour: REACHABLE -> rewrite MACs -> tx batch[egress]
                    unresolved -> hold (<=3) and ARP, or drop(neighbour_unresolved)
 flush tx batches per egress port (pio.tx_burst)
```

Every drop increments exactly one named counter (`docs/drop-reasons.md`), and
`total == sum(reasons)`.

## Data structures and who owns them

| Structure | Where | Size | Owner / mutated by |
|---|---|---|---|
| mbuf pool | `common/mbuf.c` | 4,096 x 2 KiB (128 B headroom), allocated once | main thread; LIFO free list |
| route table | `dp/route.c` + `dp/lpm_trie.c` | 10,000 routes; binary trie over a node pool | main thread |
| neighbour table | `dp/neigh.c` | open addressing, 2,048 slots, 1,024 entries | main thread |
| port table | `dp/port.c` | 16 ports; created at start-up only | main thread |
| counters | `dp/stats.c` | global + per-port | main thread |
| token buckets | `dp/ratelimit.c`, in `struct port` | one per ingress port | main thread |

Static neighbours give each VM port a **binding** (the source-binding check) and a `/32` host
route. Ports that share a subnet (the anycast gateway `10.0.1.1` on p1 and p2) have a connected
route with no egress, so they are reached only through those `/32` routes.

## mbuf lifecycle

1. `pio.rx_burst` allocates from the pool and fills `off/len`; on exhaustion it drains the socket
   and counts `nobuf`.
2. `forward_burst` owns every mbuf of the burst. Each one ends in exactly one place: freed by a
   drop, queued in a tx batch (the backend frees it after sending), consumed (ARP reply
   learned), or held by a neighbour entry.
3. A held mbuf (at most 3 per unresolved next hop) is released by ARP resolution (sent in
   order), by the 1 s deadline (dropped, ICMP host unreachable), by port down, or by neighbour
   or gateway-route deletion. The unit tests assert that `mbuf_in_use` returns to its baseline
   in every path, and the e2e suite asserts the same gauge after traffic.

## Why there is no RCU in phase 1

There is one thread, and the control plane runs on it between bursts, so readers and writers
never overlap. The tables are still reached through single owners (one route table, per-port
stats) so that phase 4, when forwarding moves to several cores, can add RCU-style table swaps
and per-core counters without changing the packet-path logic. The first performance work
planned for then (trie -> DIR-24-8) will be the first entry in `docs/perf.md`; phase 1 makes no
performance claims.

## Lab specifics that shape the code

- The kernel hands an AF_PACKET socket frames whose TCP/UDP checksum is still to be offloaded
  (`CHECKSUM_PARTIAL`) and GSO super-frames. Re-injected, such a frame carries a wrong checksum
  and the receiving VM drops it, so `lab/lab.sh` turns tx checksum, SG, TSO, GSO and GRO off on
  every veth end. A forwarder on a real NIC would instead complete the checksum itself.
- The kernel strips 802.1Q tags before a raw socket sees the frame; the backend asks for
  `PACKET_AUXDATA` to know it happened.
