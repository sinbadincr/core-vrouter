# The KVM lab (phase 1.5, default since feature 004)

The lab (`make lab`) where `vm1`–`vm3` are real KVM guests (CirrOS 0.6.3, virtio-net). The
namespace VMs that phase 1 used survive only as the test harness; see [lab.md](lab.md). `minivrd`, `lab/cn1.conf` and `vrctl` are **unchanged**: a guest's data NIC is
a TAP device named `p1`–`p3` in `mvr-cn1`, which the forwarder opens with AF_PACKET like any
other port. It is the same attachment nova/libvirt use between a VM and a vRouter on a compute node.

## Prerequisites

```bash
sudo apt install qemu-system-x86 qemu-utils genisoimage e2fsprogs openssh-client
scripts/check-prereqs.sh --kvm     # exit 2 tools missing, 3 no KVM, 4 KVM not usable by you
```

- Hardware virtualization. In VMware enable "Virtualize Intel VT-x/EPT" for the VM (nested KVM).
  There is no software (TCG) fallback: without `/dev/kvm` the command stops with exit 3.
- Your user can open `/dev/kvm` (ACL or group `kvm`). Run `sudo` from your own user: QEMU is
  started with your uid, never as root.
- About 768 MB of free memory (3 guests × 192 MB; measured ≈ 200 MB RSS each).

## Commands

```bash
sudo make lab                # image fetch+verify, namespaces, TAPs, boot 3 guests, wait for ready
sudo make lab-run            # start minivrd
sudo lab/lab.sh status       # includes a guest table: state, pid, port, MAC, addresses, console
sudo lab/lab.sh ssh vm1      # shell in a guest over the management network (or: ssh vm1 CMD)
lab/lab.sh console vm1 -f    # follow the guest's console log
sudo make e2e-kvm            # non-interactive suite; SKIP (exit 0) when the host has no KVM
sudo make lab-clean          # stop guests, delete every mvr-* object, keep logs and image cache
```

`lab.sh up` is idempotent: ready guests are left alone, stopped or failed ones are rebooted on
their existing TAP, so the forwarder keeps running and the port comes back up by itself.
`lab.sh up` and `lab.sh up --harness` refuse each other until `make lab-clean`.

## How one guest is wired

```
 root namespace: nothing lab-related (only the launcher and QEMU processes)
 ┌── mvr-cn1 ───────────────────────┐        ┌── mvr-mgmt ───────────────────┐
 │ minivrd ── AF_PACKET ── p1 (TAP) │        │ br0 10.99.0.1/24 ── m1 (TAP)  │
 └──────────────────────────┬───────┘        └──────────────┬────────────────┘
                     fd 3 + vhostfd 4                fd 5 + vhostfd 6
                            └────── QEMU mvr-vm1 (uid = you, -sandbox on) ────────┘
                                    eth0 02:00:00:00:01:01 10.0.1.11/24 gw 10.0.1.1
                                    eth1 02:00:00:00:02:01 10.99.0.11/24 (no gateway)
```

| Step here | What nova/libvirt do |
|---|---|
| `lab.sh` creates persistent TAPs (`vnet_hdr`, owned by you) | the OVS/vRouter plugin creates a tap per port |
| `lab/kvm/launch.py` (root) enters the namespaces, opens each TAP and `/dev/vhost-net`, drops to your uid with `no_new_privs`, then `exec`s QEMU | libvirtd opens the descriptors and passes them to an unprivileged QEMU |
| `-netdev tap,fd=N,vhost=on,vhostfd=M` + `virtio-net-pci` | the same libvirt `<interface type='ethernet'>` plumbing |
| config drive ISO (label `config-2`, OpenStack layout) with `user_data` and the lab key | nova config drive / metadata service |

All virtio offloads are off (`csum=off,gso=off,host_tso*=off,…`), so frames reach the forwarder
complete and at most MTU sized (phase 1 hit the same trap with veth).

## Management network

Each guest has a second NIC on `mvr-mgmt` (bridge `br0`, `10.99.0.0/24`). SSH and the e2e suite use
it, and it has no route to the data addresses: the guests have **no gateway on `eth1`**, and the
suite sniffs `br0` while data tests run and requires zero `10.0.0.0/16` frames (K11). The namespace
is reachable only through `ip netns exec mvr-mgmt`.

## Images: golden image, versioned layer, guest overlay

```
golden image   cirros-0.6.3-x86_64-disk.img      pinned by file SHA-256, never written
   ← layer     cirros-0.6.3.mvr<REV>.<hash8>.qcow2   thin qcow2 (~9 MB), recipe in git
      ← guest  build/lab/kvm/vmN/disk.qcow2          throw-away overlay, one per guest
```

qcow2 stores a *backing file*: a read goes to the top image first and falls through to the one
below, a write always lands in the top image. So the layer holds only the clusters that differ from
the golden image, and each guest only what that guest wrote.

**Why a layer.** The stock CirrOS runs `dhcpcd -w -t 180` before anything else and waits up to 180 s
for a DHCP server on every NIC. The lab has none (and the forwarder must drop DHCP broadcasts), so
a guest was ready only after 182 s. `lab/kvm/image-patch/` holds one replacement init script
(`etc/init.d/S40network`, no DHCP client). Guests are ready about 3 s after QEMU starts.

**How it is built** (`lab/kvm/image.py`, standard library, no root needed): the guest OS lives in
the initramfs inside `/boot/initrd.img-*`, so the recipe is appended to it as one more cpio archive
(the kernel unpacks concatenated archives and later entries win; an uncompressed archive must start
on a 4-byte boundary, which the builder pads for) and written back into the ext4 partition with
`debugfs`. Only differing 64 KiB clusters are written into the layer.

**Three pins** in `lab/kvm/image.lock`, checked before every bring-up:

| Pin | Meaning |
|---|---|
| `CIRROS_SHA256` | the golden file |
| `MVR_PATCH_SHA256` | hash of the recipe tree `lab/kvm/image-patch/` (`make check` fails if the recipe changes and the lock does not) |
| `MVR_LAYER_LOGICAL_SHA256` | what a guest *reads* (the layer flattened to raw). The qcow2 file's own bytes are not pinned: cluster layout is an encoding detail. The build is reproducible, so this hash is a real pin |

A damaged layer is deleted and rebuilt once; a rebuild that does not match the pin is an error.
Layer files are immutable (running guests hold them open); revisions are siblings over the golden
image, not a stack, and stay in the cache until `make clean`. Each layer has a `.json` manifest
(base, REV, recipe hash, logical hash, `qemu-img` and `debugfs` versions, build time).

**Cutting a new revision**

1. Change files under `lab/kvm/image-patch/`.
2. Bump `MVR_REV`; run `lab/kvm/image.py patch-hash --patch-dir lab/kvm/image-patch` and
   `lab/kvm/image.py build --golden build/cache/images/<CIRROS_FILE> --patch-dir lab/kvm/image-patch
   --out /tmp/l.qcow2` and put the two printed hashes into `image.lock`.
3. Boot a guest and read its console log before committing (a unit test cannot see whether the
   kernel accepted the recipe).
4. Add a row below and commit the recipe, the lock and this table together.

| REV | Date | Change | Why |
|---|---|---|---|
| 1 | 2026-11-19 | `S40network` without `dhcpcd` | boot waited 180 s for DHCP; the lab has no DHCP server |

`make lab-clean` keeps the cache and the console logs; `make clean` removes them.
Overrides for tests: `MINIVR_KVM_DEVICE`, `MINIVR_KVM_CACHE`, `MINIVR_KVM_BOOT_TIMEOUT`.

## Troubleshooting

Read `lab/lab.sh console vmN` (the file is `build/lab/kvm/logs/vmN.console.log`; QEMU's stderr is
`vmN.qemu.log`, launcher errors `vmN.launch.log`).

| Console shows | Cause |
|---|---|
| `Starting network: dhcpcd-9.4.1 starting` | the image layer was not applied (stock script); the guest then waits 180 s |
| `Initramfs unpacking failed: invalid magic` | the appended recipe archive is misaligned or corrupt |
| `json2fstree failed`, `failed 20/20` | config drive missing or unreadable; CirrOS waits ~40 s for EC2 metadata |
| `MVR-FAILED vmN step=<n> <what>` | step n of `lab/kvm/guest-init.sh.in` failed (1 IPv6, 2 data NIC, 3 mgmt NIC, 4 passwords/SSH) |
| no `MVR-READY` after 120 s | slow host or no KVM acceleration; see `vmN.qemu.log` |

## Security

QEMU runs as your uid with `-sandbox on` (seccomp), `-nodefaults`, no display, after the launcher
dropped supplementary groups and set `no_new_privs`. The SSH key pair lives in `/run/mvr-kvm/`
(root, 0700) and is removed at teardown; guest passwords are locked and `dropbear -s` refuses
password logins.
