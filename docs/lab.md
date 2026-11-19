# The minivr lab

A single-host lab: one **compute-node** namespace (`mvr-cn1`, where `minivrd` runs) and three
**VM** namespaces connected by veth pairs. It mimics a vRouter host with three guests.

```
 mvr-vm1                    mvr-cn1 (compute node)                    mvr-vm3
 eth0 10.0.1.11/24 ──veth── p1 ┐                                ┌ p3 ──veth── eth0 10.0.2.13/24
 gw 10.0.1.1                   │      minivrd (AF_PACKET)       │              gw 10.0.2.1
 mvr-vm2                       ├── gateway 10.0.1.1/24 on p1,p2 ┤
 eth0 10.0.1.12/24 ──veth── p2 ┘   gateway 10.0.2.1/24 on p3   │
 gw 10.0.1.1
```

p1 and p2 both carry the gateway `10.0.1.1/24`: an **anycast gateway**, as vRouter presents on
every VM interface. The ports in `mvr-cn1` have **no IPv4 address**; the gateway IPs exist only
in the forwarder's config, so the kernel never answers ARP or forwards (`ip_forward=0`,
`arp_ignore=8`, IPv6 off). `tcpdump` in a VM therefore proves that `minivrd` answered.

## Addressing

| Namespace | Interface | MAC | Address | Gateway |
|---|---|---|---|---|
| `mvr-vm1` | `eth0` | `02:00:00:00:01:01` | `10.0.1.11/24` | `10.0.1.1` |
| `mvr-vm2` | `eth0` | `02:00:00:00:01:02` | `10.0.1.12/24` | `10.0.1.1` |
| `mvr-vm3` | `eth0` | `02:00:00:00:01:03` | `10.0.2.13/24` | `10.0.2.1` |
| `mvr-cn1` | `p1`, `p2`, `p3` | `02:00:00:00:00:01..03` | none | n/a |

## Commands

| Command | Effect |
|---|---|
| `sudo make lab` / `sudo lab/lab.sh up` | create the topology; idempotent, repairs partial state |
| `sudo make lab-run` / `sudo lab/lab.sh run [--config FILE]` | start `minivrd` in `mvr-cn1` (default `lab/cn1.conf`); replaces a running one |
| `sudo make lab-clean` / `sudo lab/lab.sh down` | stop the forwarder, remove every `mvr-*` object and `/run/mvr-cn1/` |
| `lab/lab.sh status` | namespaces, links, forwarder pid |
| `sudo lab/lab.sh snapshot` | normalised host network state (JSON) for before/after diffs |

`minivrd` listens on `/run/mvr-cn1/minivr.sock` in the lab:

```bash
export MINIVR_SOCK=/run/mvr-cn1/minivr.sock
sudo -E build/debug/vrctl route show
```

## Safety guarantees (FR-002 to FR-004)

- Every object the lab creates is named `mvr-*` (namespaces, veths, `/run/mvr-cn1/`). The
  scripts never reference the host NIC (`ens33`) or any root-namespace route.
- `lab.sh` refuses to run if a root-namespace link called `mvr-*` exists and is not a veth.
- `up` and `down` are idempotent and tolerate partial state.
- `sudo lab/lab.sh snapshot` before `up` and after `down` must be byte-identical; the e2e suite
  checks this for the whole session.

## Inspecting

```bash
sudo ip netns exec mvr-vm1 ping -c3 10.0.1.12
sudo ip netns exec mvr-vm1 tcpdump -ni eth0 arp
sudo ip netns exec mvr-cn1 ip -d link show p1
tail -f build/lab/minivrd.log
```

## KVM mode

`sudo make lab-kvm` replaces the three VM namespaces with KVM guests on TAP ports `p1`–`p3`; the
forwarder, its config and `vrctl` are the same. Prerequisites, wiring, the management network and
troubleshooting are in [kvm.md](kvm.md). `sudo make lab-clean` removes either mode.
