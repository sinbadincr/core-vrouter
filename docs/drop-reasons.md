# Drop reasons

Every packet `minivrd` discards increments exactly one named reason counter, and
`total == sum(reasons)` always holds (`vrctl -j stats show`, `.data.drops`). The counter is
also added to the **ingress** port's `drops`. Counters are cleared by `vrctl stats clear`.

"ICMP" is the error sent back to the sender; it is rate-limited per ingress port (default
100/s, burst 20) and a rate-limited error counts as `events.icmp_ratelimited`, not as a drop.
No ICMP error is ever sent in reply to an ICMP error or to a source of `0.0.0.0`, loopback or
multicast.

Checks run in this order: Ethernet → anti-spoofing → ARP / IPv4 validation → local delivery →
TTL → route → egress state → MTU → neighbour. The first failing check names the reason.

| Reason | Trigger | ICMP sent | Covered by |
|---|---|---|---|
| `truncated` | frame shorter than the headers it claims (Ethernet < 14, IPv4 < 20, echo < 8) | none | `test_pkt_eth_ipv4` (every boundary), `test_forward`, `corpus.py` |
| `bad_ip_version` | IPv4 version ≠ 4 | none | `test_pkt_eth_ipv4`, `corpus.py` |
| `bad_ip_hdr_len` | IHL < 5, or header longer than the frame | none | `test_pkt_eth_ipv4`, `corpus.py` |
| `ip_options` | IHL > 5 (options unsupported) | none | `test_pkt_eth_ipv4`, `corpus.py` |
| `bad_ip_total_len` | total length < header length or > frame | none | `test_pkt_eth_ipv4`, `corpus.py` |
| `bad_ip_checksum` | header checksum wrong | none | `test_pkt_eth_ipv4`, `corpus.py` |
| `ip_fragment` | MF set or fragment offset ≠ 0 | none | `test_pkt_eth_ipv4`, `corpus.py` |
| `unsupported_ethertype` | IPv6, VLAN, anything that is not IPv4 or ARP | none | `test_pkt_eth_ipv4`, `test_forward`, `corpus.py` |
| `non_arp_bcast_mcast` | broadcast or multicast destination MAC that is not ARP | none | `test_pkt_eth_ipv4`, `test_forward`, `corpus.py` |
| `not_for_us_l2` | unicast destination MAC ≠ the ingress port's MAC | none | `test_pkt_eth_ipv4`, `test_forward`, `corpus.py` |
| `bad_arp` | malformed ARP: htype ≠ 1, ptype ≠ 0x0800, hlen ≠ 6, plen ≠ 4, op ∉ {1, 2}, or short | none | `test_pkt_arp`, `corpus.py` |
| `arp_unknown_target` | ARP request for an IP that is neither the port's gateway nor a known neighbour | none | `test_forward`, `test_forwarding.py::v3` |
| `source_spoofed` | on a bound port: Ethernet src MAC, ARP sender MAC/IP or IPv4 src IP ≠ the bound VM (includes using the gateway IP as a source) | none | `test_antispoof`, `test_forward`, `test_forwarding.py::v12` |
| `no_route` | no matching route (ICMP net unreachable), or a multi-port subnet with no `/32` for the host (host unreachable) | type 3 code 0 / code 1 | `test_forward`, `test_forwarding.py::v5` |
| `ttl_exceeded` | TTL ≤ 1 on a packet that must be forwarded | type 11 code 0 | `test_forward`, `test_forwarding.py::v6` |
| `mtu_exceeded` | IP length > egress MTU (never fragmented) | type 3 code 4 with the MTU, only if DF is set | `test_forward` |
| `neighbour_unresolved` | hold queue full (3), entry `FAILED`, or the 1 s resolution deadline passed (held packets) | type 3 code 1 when an entry fails; none when the hold queue is full | `test_neigh`, `test_forwarding.py::v16` |
| `port_down` | egress port admin- or oper-down, or its port went down while packets were held | none | `test_forward`, `test_neigh`, `test_robustness.py` |
| `local_unsupported` | addressed to a forwarder address and not an ICMP echo request | none | `test_forward` |
| `nobuf` | mbuf pool (4,096) exhausted on receive | none | AF_PACKET backend; drained so the socket cannot stay readable |
| `tx_error` | the kernel refused a transmit | none | AF_PACKET backend |

Not drops: ARP replies and gratuitous ARP consumed by the forwarder, echo replies, and ICMP
errors the forwarder generates (`events.icmp_sent`, `events.arp_replies_sent`).
