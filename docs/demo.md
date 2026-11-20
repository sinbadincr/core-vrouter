# minivr demo transcript

Output of `sudo scripts/demo.sh`: the KVM lab (three CirrOS guests on TAP ports), forwarding,
drops, a live route change and teardown. Captured on the development host with KVM guests on
2026-11-19. PIDs and timings vary. A terminal recording is `docs/demo.cast`
(`sudo asciinema rec --overwrite -c scripts/demo.sh docs/demo.cast`).

```text

# 1. one command: three KVM guests on TAP ports p1-p3
$ lab/lab.sh up
prerequisites OK
lab up (kvm): mvr-cn1 + vm1..3, mgmt mvr-mgmt

# 2. the forwarder (lab/cn1.conf); it drops to nobody once it is set up
$ lab/lab.sh run
forwarder running (pid 410499), log /home/tri/repo/greennode/build/lab/minivrd.log
alias vrctl='/home/tri/repo/greennode/build/debug/vrctl -s /run/mvr-cn1/minivr.sock'    # or: export MINIVR_SOCK=/run/mvr-cn1/minivr.sock
$ lab/lab.sh status
lab: kvm
== mvr-mgmt
br0              UP             8e:e6:50:40:97:61 <BROADCAST,MULTICAST,UP,LOWER_UP> 
m1               UP             9a:34:22:c7:0e:77 <BROADCAST,MULTICAST,UP,LOWER_UP> 
m2               UP             ae:36:b9:4d:2a:97 <BROADCAST,MULTICAST,UP,LOWER_UP> 
m3               UP             8e:e6:50:40:97:61 <BROADCAST,MULTICAST,UP,LOWER_UP> 
br0              UP             10.99.0.1/24 
m1               UP             
m2               UP             
m3               UP             
== mvr-cn1
p1               UP             02:00:00:00:00:01 <BROADCAST,MULTICAST,UP,LOWER_UP> 
p2               UP             02:00:00:00:00:02 <BROADCAST,MULTICAST,UP,LOWER_UP> 
p3               UP             02:00:00:00:00:03 <BROADCAST,MULTICAST,UP,LOWER_UP> 
p1               UP             
p2               UP             
p3               UP             
forwarder pid: 410499
== guests (mode kvm)
NAME  STATE    PID    PORT  MAC                DATA          MGMT         CONSOLE
vm1   ready    410293 p1    02:00:00:00:01:01  10.0.1.11/24  10.99.0.11   build/lab/kvm/logs/vm1.console.log
vm2   ready    410334 p2    02:00:00:00:01:02  10.0.1.12/24  10.99.0.12   build/lab/kvm/logs/vm2.console.log
vm3   ready    410373 p3    02:00:00:00:01:03  10.0.2.13/24  10.99.0.13   build/lab/kvm/logs/vm3.console.log

# 3. guests reach each other through the forwarder (TTL 63 = one hop)
$ [vm1] sudo ping -c3 -W1 10.0.2.13
PING 10.0.2.13 (10.0.2.13) 56(84) bytes of data.
64 bytes from 10.0.2.13: icmp_seq=1 ttl=63 time=0.435 ms
64 bytes from 10.0.2.13: icmp_seq=2 ttl=63 time=1.29 ms
64 bytes from 10.0.2.13: icmp_seq=3 ttl=63 time=2.01 ms

--- 10.0.2.13 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2031ms
rtt min/avg/max/mdev = 0.435/1.245/2.007/0.642 ms
$ [vm3] sudo ping -c3 -W1 10.0.1.12
PING 10.0.1.12 (10.0.1.12) 56(84) bytes of data.
64 bytes from 10.0.1.12: icmp_seq=1 ttl=63 time=0.456 ms
64 bytes from 10.0.1.12: icmp_seq=2 ttl=63 time=0.718 ms
64 bytes from 10.0.1.12: icmp_seq=3 ttl=63 time=2.22 ms

--- 10.0.1.12 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2051ms
rtt min/avg/max/mdev = 0.456/1.132/2.224/0.779 ms

# 4. TTL 1 expires at the forwarder; an unknown network is unreachable
$ [vm1] ping -c1 -t1 -W2 10.0.2.13
PING 10.0.2.13 (10.0.2.13) 56(84) bytes of data.
From 10.0.1.1 icmp_seq=1 Time to live exceeded

--- 10.0.2.13 ping statistics ---
1 packets transmitted, 0 received, +1 errors, 100% packet loss, time 0ms

$ [vm1] ping -c1 -W2 10.9.9.9
PING 10.9.9.9 (10.9.9.9) 56(84) bytes of data.
From 10.0.1.1 icmp_seq=1 Destination Net Unreachable

--- 10.9.9.9 ping statistics ---
1 packets transmitted, 0 received, +1 errors, 100% packet loss, time 0ms


# 5. a spoofed source address is dropped and counted
$ [vm1] sudo ip addr add 10.0.1.12/32 dev eth0
$ [vm1] ping -c3 -W1 -I 10.0.1.12 10.0.2.13
PING 10.0.2.13 (10.0.2.13) from 10.0.1.12 : 56(84) bytes of data.

--- 10.0.2.13 ping statistics ---
3 packets transmitted, 0 received, 100% packet loss, time 2035ms

$ [vm1] sudo ip addr del 10.0.1.12/32 dev eth0
$ build/debug/vrctl -j stats show | jq .data.drops.reasons.source_spoofed
3

# 6. change routes while traffic flows (no restart; same forwarder pid)
$ forwarder pid: 410499
$ [vm3] sudo ip addr add 10.0.9.1/32 dev lo
$ build/debug/vrctl route add 10.0.9.0/24 dev p3 via 10.0.2.13
ok
$ [vm1] ping -c1 -W2 10.0.9.1
PING 10.0.9.1 (10.0.9.1) 56(84) bytes of data.

--- 10.0.9.1 ping statistics ---
1 packets transmitted, 0 received, 100% packet loss, time 0ms

$ build/debug/vrctl route del 10.0.9.0/24
ok
$ [vm1] ping -c1 -W1 10.0.9.1
PING 10.0.9.1 (10.0.9.1) 56(84) bytes of data.
From 10.0.1.1 icmp_seq=1 Destination Net Unreachable

--- 10.0.9.1 ping statistics ---
1 packets transmitted, 0 received, +1 errors, 100% packet loss, time 0ms

$ build/debug/vrctl route add 10.0.9.0/24 dev p3 via 10.0.2.13
ok
$ [vm1] ping -c1 -W1 10.0.9.1
PING 10.0.9.1 (10.0.9.1) 56(84) bytes of data.

--- 10.0.9.1 ping statistics ---
1 packets transmitted, 0 received, 100% packet loss, time 0ms

$ forwarder pid: 410499

# 7. inspect state
$ build/debug/vrctl route show
PREFIX          PORT  GATEWAY     ORIGIN
10.0.1.0/24     -     -           connected
10.0.1.11/32    p1    -           host
10.0.1.12/32    p2    -           host
10.0.2.0/24     p3    -           connected
10.0.2.13/32    p3    -           host
10.0.9.0/24     p3    10.0.2.13   static
$ build/debug/vrctl neigh show
IP              MAC                PORT  KIND     STATE       HELD
10.0.1.11       02:00:00:00:01:01  p1    static   REACHABLE   0
10.0.1.12       02:00:00:00:01:02  p2    static   REACHABLE   0
10.0.2.13       02:00:00:00:01:03  p3    static   REACHABLE   0
$ build/debug/vrctl port show
NAME  IFNAME  MAC                IP               MTU   ADMIN OPER     RX_PKTS    RX_BYTES   TX_PKTS    TX_BYTES   DROPS
p1    p1      02:00:00:00:00:01  10.0.1.1/24      1500  up    up            13        1218         8         644       6
p2    p2      02:00:00:00:00:02  10.0.1.1/24      1500  up    up             5         434         5         434       0
p3    p3      02:00:00:00:00:03  10.0.2.1/24      1500  up    up            10         924        10         924       2
$ build/debug/vrctl stats show
PORT     RX_PKTS    RX_BYTES   TX_PKTS    TX_BYTES   DROPS
p1            13        1218         8         644       6
p2             5         434         5         434       0
p3            10         924        10         924       2

DROPS total 8
  truncated               0
  bad_ip_version          0
  bad_ip_hdr_len          0
  ip_options              0
  bad_ip_total_len        0
  bad_ip_checksum         0
  ip_fragment             0
  unsupported_ethertype   0
  non_arp_bcast_mcast     0
  not_for_us_l2           0
  bad_arp                 0
  arp_unknown_target      0
  source_spoofed          5
  no_route                2
  ttl_exceeded            1
  mtu_exceeded            0
  neighbour_unresolved    0
  port_down               0
  local_unsupported       0
  nobuf                   0
  tx_error                0

EVENTS
  icmp_ratelimited        0
  neighbour_table_full    0
  arp_replies_sent        3
  icmp_sent               3

MBUF in_use 0 / 4096

# 8. one command removes everything; the host network is untouched
$ lab/lab.sh down
lab down
```
