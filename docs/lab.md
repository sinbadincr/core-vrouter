# The minivr lab

A single-host lab: one **compute-node** namespace (`mvr-cn1`, where `minivrd` runs), three
**KVM guests** (`vm1`–`vm3`, CirrOS, virtio-net) on TAP ports `p1`–`p3`, and a management
namespace `mvr-mgmt` (bridge `br0`, `10.99.0.0/24`) the lab uses to reach the guests over SSH.
It mimics a vRouter host with three guests. The lab needs `/dev/kvm`; without it `make lab` exits
3 or 4 and creates nothing. Prerequisites, wiring, images and troubleshooting: [kvm.md](kvm.md).

```
 vm1 (KVM)                  mvr-cn1 (compute node)                    vm3 (KVM)
 eth0 10.0.1.11/24 ──TAP── p1 ┐                                ┌ p3 ──TAP── eth0 10.0.2.13/24
 gw 10.0.1.1                  │      minivrd (AF_PACKET)       │              gw 10.0.2.1
 vm2 (KVM)                    ├── gateway 10.0.1.1/24 on p1,p2 ┤
 eth0 10.0.1.12/24 ──TAP── p2 ┘   gateway 10.0.2.1/24 on p3   │
 gw 10.0.1.1                 (each guest also has eth1 on mvr-mgmt, no gateway)
```

p1 and p2 both carry the gateway `10.0.1.1/24`: an **anycast gateway**, as vRouter presents on
every VM interface. The ports in `mvr-cn1` have **no IPv4 address**; the gateway IPs exist only
in the forwarder's config, so the kernel never answers ARP or forwards (`ip_forward=0`,
`arp_ignore=8`, IPv6 off). `tcpdump` in a guest therefore proves that `minivrd` answered.

## Addressing

| Where | Interface | MAC | Address | Gateway |
|---|---|---|---|---|
| guest `vm1` | `eth0` | `02:00:00:00:01:01` | `10.0.1.11/24` | `10.0.1.1` |
| guest `vm2` | `eth0` | `02:00:00:00:01:02` | `10.0.1.12/24` | `10.0.1.1` |
| guest `vm3` | `eth0` | `02:00:00:00:01:03` | `10.0.2.13/24` | `10.0.2.1` |
| `mvr-cn1` | `p1`, `p2`, `p3` | `02:00:00:00:00:01..03` | none | n/a |

## Commands

| Command | Effect |
|---|---|
| `sudo make lab` / `sudo lab/lab.sh up` | create the KVM lab (image, TAPs, guests, wait for ready); idempotent, repairs partial state. Exit 3 without KVM, 4 if KVM is not usable by you, 2 for missing tools |
| `sudo make lab-run` / `sudo lab/lab.sh run [--config FILE]` | start `minivrd` in `mvr-cn1` (default `lab/cn1.conf`); replaces a running one |
| `sudo make lab-clean` / `sudo lab/lab.sh down` | stop the forwarder, remove every `mvr-*` object and `/run/mvr-cn1/` |
| `lab/lab.sh status` | `lab: kvm` (or `harness`, `none`), namespaces, links, forwarder pid, guest table |
| `sudo lab/lab.sh ssh vm1 [CMD...]` | shell or command in a guest over the management network |
| `lab/lab.sh console vm1 [-f]` | the guest's console log |
| `sudo lab/lab.sh snapshot` | normalised host network state (JSON) for before/after diffs |

`minivrd` listens on `/run/mvr-cn1/minivr.sock` in the lab:

```bash
export MINIVR_SOCK=/run/mvr-cn1/minivr.sock
sudo -E build/debug/vrctl route show
```

## Safety guarantees (FR-002 to FR-004)

- Every object the lab creates is named `mvr-*` (namespaces, TAPs, veths, `/run/mvr-cn1/`). The
  scripts never reference the host NIC (`ens33`) or any root-namespace route.
- `lab.sh` refuses to run if a root-namespace link called `mvr-*` exists and is not a veth.
- `up` and `down` are idempotent and tolerate partial state.
- `sudo lab/lab.sh snapshot` before `up` and after `down` must be byte-identical; the e2e suite
  checks this for the whole session.

## Inspecting

```bash
sudo lab/lab.sh ssh vm1 ping -c3 10.0.1.12
sudo lab/lab.sh ssh vm1 sudo tcpdump -ni eth0 arp
sudo ip netns exec mvr-cn1 ip -d link show p1
tail -f build/lab/minivrd.log
```

## Test harness (namespace VMs)

`sudo lab/lab.sh up --harness` builds the phase 1 topology instead: three namespaces `mvr-vm1..3`
joined to `mvr-cn1` by veth pairs, with the same addresses and MACs as above. It is what
`sudo make e2e` uses. It exists because some tests must put frames on the wire that no guest
kernel would send (malformed ARP/IPv4, spoofed addresses, truncated packets) and because it runs
on a host without KVM. It is **not** the lab: the README, the demo and `make lab` never use it,
and there is no `make` target for it.

The harness and the KVM lab exclude each other: `up` while the harness is up and `up --harness`
while the KVM lab is up both stop with exit 1 until `sudo make lab-clean`, which removes either.
`lab.sh status` prints `lab: harness` as its first line. Which tests run where, and why, is in
[traceability.md](traceability.md).
