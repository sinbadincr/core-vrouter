# minivr demo transcript

Output of `sudo scripts/demo.sh` (a scripted run through the lab, forwarding, live route
changes, the malformed-traffic corpus and teardown). To record a terminal cast instead:
`sudo asciinema rec -c scripts/demo.sh docs/demo.cast`.

This transcript was captured inside an unprivileged user and network namespace, so the privilege
drop to `nobody` (step 2 prints it on a real host) is not shown; it is verified by
`tests/e2e/test_privileges.py`. Timings and PIDs vary.

```text
# 1. one command builds the lab: one compute node, three VMs
$ lab/lab.sh up
lab up: mvr-cn1 + mvr-vm1..3

# 2. start the forwarder (lab/cn1.conf); it drops to nobody once it is set up
$ lab/lab.sh run
forwarder running (pid 4242), log build/lab/minivrd.log

# 3. VMs reach each other through the forwarder (TTL 63 = one hop)
$ [mvr-vm1] ping -c3 -W1 10.0.2.13
PING 10.0.2.13 (10.0.2.13) 56(84) bytes of data.
64 bytes from 10.0.2.13: icmp_seq=1 ttl=63 time=0.136 ms
64 bytes from 10.0.2.13: icmp_seq=2 ttl=63 time=0.169 ms
64 bytes from 10.0.2.13: icmp_seq=3 ttl=63 time=0.415 ms

--- 10.0.2.13 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2052ms
rtt min/avg/max/mdev = 0.136/0.240/0.415/0.124 ms
$ [mvr-vm1] ping -c2 -W1 10.0.1.12
PING 10.0.1.12 (10.0.1.12) 56(84) bytes of data.
64 bytes from 10.0.1.12: icmp_seq=1 ttl=63 time=1.19 ms
64 bytes from 10.0.1.12: icmp_seq=2 ttl=63 time=0.587 ms

--- 10.0.1.12 ping statistics ---
2 packets transmitted, 2 received, 0% packet loss, time 1002ms
rtt min/avg/max/mdev = 0.587/0.887/1.187/0.300 ms

#    the gateway answers for itself, and ARP is answered by the forwarder
$ [mvr-vm1] ping -c1 -W1 10.0.1.1
PING 10.0.1.1 (10.0.1.1) 56(84) bytes of data.
64 bytes from 10.0.1.1: icmp_seq=1 ttl=64 time=2.42 ms

--- 10.0.1.1 ping statistics ---
1 packets transmitted, 1 received, 0% packet loss, time 0ms
rtt min/avg/max/mdev = 2.418/2.418/2.418/0.000 ms

# 4. inspect state
$ build/debug/vrctl route show
PREFIX          PORT  GATEWAY     ORIGIN
10.0.1.0/24     -     -           connected
10.0.1.11/32    p1    -           host
10.0.1.12/32    p2    -           host
10.0.2.0/24     p3    -           connected
10.0.2.13/32    p3    -           host
$ build/debug/vrctl neigh show
IP              MAC                PORT  KIND     STATE       HELD
10.0.1.11       02:00:00:00:01:01  p1    static   REACHABLE   0
10.0.1.12       02:00:00:00:01:02  p2    static   REACHABLE   0
10.0.2.13       02:00:00:00:01:03  p3    static   REACHABLE   0
$ build/debug/vrctl port show
NAME  IFNAME  MAC                IP               MTU   ADMIN OPER     RX_PKTS    RX_BYTES   TX_PKTS    TX_BYTES   DROPS
p1    p1      02:00:00:00:00:01  10.0.1.1/24      1500  up    up             8         672         8         672       0
p2    p2      02:00:00:00:00:02  10.0.1.1/24      1500  up    up             3         238         3         238       0
p3    p3      02:00:00:00:00:03  10.0.2.1/24      1500  up    up             4         336         4         336       0

# 5. change routes while traffic flows (no restart)
$ [mvr-vm3] ip addr add 10.0.9.1/32 dev lo
$ lab/lab.sh run --config lab/cn1-nobind.conf
forwarder running (pid 4242), log build/lab/minivrd.log
$ build/debug/vrctl route add 10.0.9.0/24 dev p3 via 10.0.2.13
ok
$ [mvr-vm1] ping -c1 -W2 10.0.9.1
PING 10.0.9.1 (10.0.9.1) 56(84) bytes of data.
64 bytes from 10.0.9.1: icmp_seq=1 ttl=63 time=0.143 ms

--- 10.0.9.1 ping statistics ---
1 packets transmitted, 1 received, 0% packet loss, time 0ms
rtt min/avg/max/mdev = 0.143/0.143/0.143/0.000 ms
$ build/debug/vrctl route del 10.0.9.0/24
ok
$ [mvr-vm1] ping -c1 -W1 10.0.9.1
PING 10.0.9.1 (10.0.9.1) 56(84) bytes of data.
From 10.0.1.1 icmp_seq=1 Destination Net Unreachable

--- 10.0.9.1 ping statistics ---
1 packets transmitted, 0 received, +1 errors, 100% packet loss, time 0ms

$ build/debug/vrctl route add 10.0.9.0/24 dev p3 via 10.0.2.13
ok
$ [mvr-vm1] ping -c1 -W1 10.0.9.1
PING 10.0.9.1 (10.0.9.1) 56(84) bytes of data.
64 bytes from 10.0.9.1: icmp_seq=1 ttl=63 time=0.132 ms

--- 10.0.9.1 ping statistics ---
1 packets transmitted, 1 received, 0% packet loss, time 0ms
rtt min/avg/max/mdev = 0.132/0.132/0.132/0.000 ms

# 6. malformed traffic: every frame lands in a named drop reason
$ build/debug/vrctl stats clear
ok
$ .venv/bin/python tests/e2e/corpus.py --ns mvr-vm1 --count 3
{"expected": {"truncated": 3, "bad_ip_version": 3, "bad_ip_hdr_len": 6, "ip_options": 3, "bad_ip_total_len": 6, "bad_ip_checksum": 3, "ip_fragment": 6, "unsupported_ethertype": 6, "non_arp_bcast_mcast": 6, "not_for_us_l2": 3, "bad_arp": 15, "arp_unknown_target": 3, "source_spoofed": 12}, "sent": 75}
$ build/debug/vrctl -j stats show | jq .data.drops
{"total":75,"reasons":{"truncated":3,"bad_ip_version":3,"bad_ip_hdr_len":6,"ip_options":3,"bad_ip_total_len":6,"bad_ip_checksum":3,"ip_fragment":6,"unsupported_ethertype":6,"non_arp_bcast_mcast":6,"not_for_us_l2":3,"bad_arp":15,"arp_unknown_target":3,"source_spoofed":12}}

# 7. the forwarder is still up and still forwarding
$ [mvr-vm1] ping -c1 -W1 10.0.9.1
PING 10.0.9.1 (10.0.9.1) 56(84) bytes of data.
64 bytes from 10.0.9.1: icmp_seq=1 ttl=63 time=0.107 ms

--- 10.0.9.1 ping statistics ---
1 packets transmitted, 1 received, 0% packet loss, time 0ms
rtt min/avg/max/mdev = 0.107/0.107/0.107/0.000 ms
$ build/debug/vrctl version
minivrd 0.1.0 (debug build) protocol 1 uptime 3s

# 8. one command removes everything; the host network is untouched
$ lab/lab.sh down
lab down
```
