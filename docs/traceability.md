# Requirement traceability

Every functional requirement (FR) and success criterion (SC) of
`specs/001-lab-l3-forwarder/spec.md` and the automated check that covers it. Unit tests live in
`tests/unit/` (ctest), end-to-end tests in `tests/e2e/` (`sudo make e2e`).

"needs root" means the e2e test skips itself where it cannot switch users or drop to `nobody`
(for example inside an unprivileged user namespace) and must be run with `sudo make e2e` on a real host.

## Functional requirements

| FR | Requirement (short) | Covered by |
|---|---|---|
| FR-001 | one command builds 1 compute node + 3 VMs | `test_lab.py::test_topology_matches_contract` |
| FR-002 | idempotent; touches only `mvr-*` objects | `test_lab.py::test_up_is_idempotent`, `test_down_after_partial_up`, `test_up_recovers_from_partial_state`, `test_down_removes_every_lab_object` |
| FR-003 | never modifies the host NIC or its routes | `test_lab.py::test_host_state_unchanged_and_ens33_untouched`, `test_scripts_never_reference_the_host_nic`; whole-session snapshot diff in `conftest.py` |
| FR-004 | kernel does not forward or answer for VM traffic | `test_lab.py::test_topology_matches_contract` (`ip_forward=0`, no IPv4 on ports, `arp_ignore=8`, IPv6 off); `test_forwarding.py::test_v2_*` (VM sees no kernel ARP) |
| FR-005 | process every frame of the configured ports in bursts | `test_forward.c::burst_of_many_frames_is_batched_without_leaks`, `test_forwarding.py::test_v4_*` |
| FR-006 | ARP reply for a port's gateway with the port MAC | `test_pkt_arp.c`, `test_forward.c::arp_for_gateway_known_vm_and_unknown`, `test_forwarding.py::test_v1_*` |
| FR-007 | proxy ARP for known neighbours only | `test_forward.c::arp_for_gateway_known_vm_and_unknown`, `test_forwarding.py::test_v2_*`, `test_v3_*` |
| FR-007a | static neighbour per lab VM loaded at start | `lab/cn1.conf`, `test_neigh.c::static_entry_is_reachable_immediately`, `test_route.c::static_neighbour_installs_and_removes_host_route`, `test_cli.py::test_table_output_matches_the_contract` |
| FR-008 | resolve unknown next hops with ARP; learned never overwrites static | `test_neigh.c::incomplete_then_reachable_*`, `learned_never_overwrites_static`, `test_forward.c::unbound_port_learns_*`, `test_forwarding.py::test_v16_*` |
| FR-008a | hold at most 3 packets, ARP retries, 1 s deadline | `test_neigh.c::fourth_packet_*`, `arp_retries_then_fails_at_the_deadline`, `port_down_and_gateway_removal_*` |
| FR-009 | longest-prefix match | `test_lpm.c` (incl. differential vs linear scan), `test_route.c`, `test_cli.py::test_v10_*` |
| FR-009a | 16 ports, 10,000 routes, 1,024 neighbours, 4,096 mbufs | `test_port.c::sixteen_ports_then_table_full`, `test_lpm.c::capacity_is_10000_rules`, `test_route.c::table_full_at_10000`, `test_neigh.c::capacity_1024`, `test_mbuf.c::pool_exhaustion_and_free` |
| FR-010 | TTL-1 with a valid incremental checksum | `test_pkt_eth_ipv4.c::ttl_decrement_matches_full_recompute`, `test_forward.c::forward_vm1_to_vm3_*`, `test_forwarding.py::test_v4_*` (ttl=63) |
| FR-011 | ICMP unreachable / time exceeded / frag needed | `test_pkt_icmp.c`, `test_forward.c` (`no_route_*`, `ttl_one_*`, `oversize_with_df_*`), `test_forwarding.py::test_v5_*`, `test_v6_*` |
| FR-011a | ICMP error token bucket per port | `test_ratelimit.c`, `test_forward.c::icmp_errors_are_rate_limited_*`, `test_robustness.py::test_v14_*` |
| FR-012 | answer echo to own addresses | `test_forward.c::echo_to_the_gateway_is_answered_locally`, `test_pkt_icmp.c`, `test_forwarding.py::test_v7_*` |
| FR-013 | validate every field against the actual length | `test_pkt_eth_ipv4.c` (truncated at every boundary), `test_pkt_arp.c`, `test_robustness.py::test_v13_*` |
| FR-014 | drop broadcast/multicast non-ARP and foreign unicast without flooding | `test_pkt_eth_ipv4.c::ethernet_classification`, `test_forward.c::malformed_frames_are_counted_once_each`, `corpus.py` |
| FR-014a | source binding / anti-spoofing | `test_antispoof.c`, `test_forward.c::spoofed_sources_*`, `test_forwarding.py::test_v12_*`, `corpus.py` |
| FR-015 | per-port counters, oper state follows the link | `test_forward.c` (rx/tx/drops per port), `test_robustness.py::test_link_flap_*` |
| FR-016 | CLI lists ports, routes, neighbours, stats; table and JSON | `test_cmd.c::show_commands_json_shapes`, `test_cli.py::test_table_output_*`, `test_json_shapes` |
| FR-017 | CLI changes routes, neighbours, counters live | `test_cmd.c::mutating_commands_work`, `test_cli.py::test_v10_*`, `test_admin_down_and_neighbour_commands` |
| FR-018 | invalid input rejected, state unchanged | `test_cmd.c::every_error_code_*`, `test_cli.py::test_errors_leave_state_unchanged`, `test_exit_codes` |
| FR-019 | structured log lines, rate-limited on the packet path | `test_log.c`, `scripts/check-fastpath.sh` (only `LOG_RL` in `src/dp`) |
| FR-020 | clean shutdown within 2 s | `test_robustness.py::test_v17_*` |
| FR-021 | debug builds leave a crash dump | `test_crash.py` |
| FR-022 | control socket only for the launching user or root | `test_ctl_framing.c` (framing), `test_cli.py::test_other_users_are_not_permitted` (**needs root**) |
| FR-022a | privileges dropped after start-up | `test_privileges.py` (**needs root**) |
| FR-023 | one command runs the e2e verification | `tests/e2e/run.sh` (`make e2e`) |
| FR-024 | documentation set | `README.md`, `docs/{architecture,lab,cli,drop-reasons,traceability}.md`, `incidents/0001-*.md` |

## Success criteria

| SC | Criterion | Covered by |
|---|---|---|
| SC-001 | clone to first ping in under 10 minutes | `README.md` quickstart; the commands are the ones `make e2e` exercises (not timed automatically) |
| SC-002 | bring-up < 10 s, teardown < 5 s, host unchanged | `test_lab.py::test_timing_budget`, `test_host_state_unchanged_*` |
| SC-003 | 1,000 pings between every VM pair, 0% loss | `test_forwarding.py::test_v4_*` |
| SC-004 | 1 GB TCP transfer byte-identical | `test_forwarding.py::test_v8_*` |
| SC-005 | route change effective within 1 s, no restart | `test_cli.py::test_v10_*` |
| SC-006 | 100% of the malformed corpus attributed, forwarding unaffected | `test_robustness.py::test_v13_*` |
| SC-007 | sum of reason counters equals total drops | `test_stats.c`, `test_forward.c`, `test_cli.py::test_v11_*`, `test_robustness.py::test_v13_*` |
| SC-008 | e2e < 2 min and green 10 times in a row | `tests/e2e/run.sh` + the 120 s budget in `conftest.py`; `scripts/e2e-soak.sh 10` (run it on the target host) |
| SC-009 | symbolized backtrace from core + debug binary | `test_crash.py`, `incidents/0001-forced-crash-drill.md` |
| SC-010 | 10,000-route lookups agree with a linear scan | `test_lpm.c::differential_against_linear_scan` |
| SC-011 | ICMP-error flood bounded, other ports unaffected | `test_robustness.py::test_v14_*` |
