# Incident 0002: KVM lab bring-up defects (phase 1.5)

| | |
|---|---|
| Date | 2026-11-19 |
| Kind | Defects found by the first real bring-ups of KVM lab mode (`002-kvm-lab-mode`) |
| Severity | none for the forwarder (lab code only, no change under `src/`); each defect blocked `make lab-kvm` |
| Build | `debug` preset; QEMU 10.0.13, CirrOS 0.6.3, kernel 5.15 in the guest |
| Status | closed: 28/28 `tests/e2e_kvm` pass, 5 runs in a row |
| Regression tests | `tests/lab/test_kvm_launch.py::FirstBoot::test_user_data`, `tests/lab/test_kvm_image.py::AppendArchive`, `tests/e2e_kvm/test_kvm_mgmt.py::test_guest_invariants`, `::test_k13_*`, `tests/e2e_kvm/test_kvm_lab.py::test_k1_*` |

Two of the defects (D4, D6) are worth a post-mortem because each was a wrong assumption that the
unit tests could not see; only a real guest boot showed it. D1-D3 and D5 are listed for completeness.

## Symptom

`sudo make lab-kvm` did not reach `lab up (kvm)`:

1. First run: `vm1 not ready after 120s` for all three guests (the console showed `dhcpcd` still
   soliciting a lease).
2. Later run: bring-up sat silent after `prerequisites OK` until interrupted; ping and SSH from
   `mvr-mgmt` to the guests timed out although the bridge and TAPs were up.
3. `make e2e-kvm`: one failure, `REMOTE HOST IDENTIFICATION HAS CHANGED` after a guest was rebuilt.

## Timeline (local time, UTC+7)

| Time | Event |
|---|---|
| 11:39 | first `make lab-kvm`: QEMU, TAPs, vhost, PCI addresses and the config drive all work; guests boot but only after 182 s (D1), and print `MVR-FAILED vm1 step=4` (D2, D3) |
| 13:35-13:54 | image layer built and measured: guest ready after 2.5 s |
| 13:54 | first layer boots but still runs `dhcpcd`: kernel log `Initramfs unpacking failed: invalid magic at start of compressed archive` (D6) |
| 14:07 | `make lab-kvm` hangs after `prerequisites OK`; guests are `MVR-READY` but SSH never connects (D4) |
| 14:20 | route fix in place; `lab-kvm` returns, `ping` vm1 -> vm3 shows `ttl=63` |
| 14:32 | `make e2e-kvm`: 27 of 28 pass, K13 fails on the host key (D5) |
| 14:40 | host key fix; `e2e-kvm` 28/28; five consecutive runs 5/5 |

## Reproduction

```bash
sudo make lab-kvm 2>&1 | tee /tmp/lab-kvm.out
sudo lab/lab.sh status
sudo ip netns exec mvr-mgmt ping -c2 -W1 10.99.0.11      # D4: 100% loss
tail -n 40 build/lab/kvm/logs/vm1.console.log            # the boot log tells which defect
```

## Logs

D4, from the guest's own boot report (`=== network info ===` in `vm1.console.log`):

```
if-info: eth1,up,10.99.0.11,24,,
ip-route:default via 10.0.1.1 dev eth0
ip-route:10.0.1.0/24 dev eth0 scope link  src 10.0.1.11
                                                     <- no 10.99.0.0/24 dev eth1
```

D6, kernel log with an uncompressed cpio appended after the initrd's gzip stream:

```
[    0.749998] Initramfs unpacking failed: invalid magic at start of compressed archive
Starting network: dhcpcd-9.4.1 starting        <- the stock script still ran
```

## Diagnosis

Each step used the evidence the guest itself printed: the console log (every guest tees its boot
to `build/lab/kvm/logs/vmN.console.log`), the `ip-route:` lines, and a QEMU run in user-mode
networking (`-netdev user`, no root) with the same config drive to test a fix in seconds.

## Root cause

| # | Defect | Root cause |
|---|---|---|
| D1 | Guests ready after 182 s | CirrOS `S40network` runs `dhcpcd --noipv4ll -w -t 180`: boot blocks until every NIC has a lease, and the lab has no DHCP server. The spike had used QEMU user-mode networking, whose built-in DHCP hid this. |
| D2 | `MVR-FAILED step=4` | `passwd -l root` exits non-zero when root is already locked, and the script treated that as failure. |
| D3 | `dropbear -s -R` failed (`Address already in use`) | `pkill dropbear` did not stop the stock dropbear; the new one could not bind port 22. |
| **D4** | ping and SSH to the guests time out | The script ran `ip route flush dev eth1` to remove a default route, which also removed the connected route `10.99.0.0/24` that `ip addr add` had just created. The guest still answered ARP (no route needed) but sent every IP reply out of `eth0` to the forwarder, which dropped it. The old unit test only allowed `route` lines containing `eth1` if they were `del` or `flush`, so it encouraged the bug. |
| D5 | `HOST IDENTIFICATION HAS CHANGED` on K13 | A rebuilt guest gets a new overlay, hence new SSH host keys; `/run/mvr-kvm/known_hosts` still held the old key. |
| **D6** | image layer accepted by hash, ignored by the kernel | The original `initrd.img` is 9,073,358 bytes (not a multiple of 4). The kernel only accepts an *uncompressed* cpio that starts on a 4-byte boundary (it skips zero bytes between archives); otherwise it rejects it as "invalid magic" and keeps the stock files. A gzip-compressed archive has no such rule, which is why the first spike worked. |

Why the tests did not catch D4 and D6: D4 needs a live guest to see the routing table, and the unit
test checked the wrong thing; D6 produced a correct, reproducible hash for a layer that the kernel
then ignored, so hash checks alone prove nothing about whether the guest applies it.

## Fix

D1: a versioned thin qcow2 layer over the pinned golden image (`lab/kvm/image.py`, recipe in
`lab/kvm/image-patch/`, three pins in `lab/kvm/image.lock`; research R15, docs/kvm.md). D2, D3:
`lab/kvm/guest-init.sh.in` ignores the root-lock status and stops dropbear with `pidof`/`kill`, then
waits for port 22. D4: `ip route del default dev eth1` instead of `flush`. D5: `ssh-keygen -R` for
the guest's address when its overlay is recreated (`kvm_seed`). D6: `append_archive()` zero-pads the
initrd to a 4-byte boundary before appending. Also: `kvm_wait_ready` now prints progress every 10 s,
the last SSH error on timeout, and bounds each probe to 8 s, because the first hang looked like a
frozen command. The fixes land in the `feat:`/`fix:` commits of branch `002-kvm-lab-mode`.

## Regression test

- D2, D3, D4 (script): `test_kvm_launch.py::FirstBoot::test_user_data` forbids `route flush` and
  allows only `ip route del default` for `eth1`.
- D4 (live): `test_kvm_mgmt.py::test_guest_invariants` requires `10.99.0.0/24 dev eth1` in each
  guest and nothing else routed via `eth1`.
- D1, D6: `test_kvm_lab.py::test_k1_*` requires `Starting network: OK (static` on the console, so
  an ignored layer fails the suite; `test_kvm_image.py::AppendArchive` covers the alignment for
  every initrd length; the logical-hash pin and `test_k17_*` cover content and tampering.
- D5: `test_kvm_mgmt.py::test_k13_killed_guest_is_stopped_and_recovers`.

## What went well / what to improve

- The three highest-risk assumptions of the design (descriptor passing to an unprivileged QEMU with
  `-sandbox on`, PCI slots 0x3/0x4 against the auto-assigned drives, vhost-net) worked on the first
  boot.
- The per-guest console log and the guest's own `=== network info ===` report made every defect
  diagnosable from one file.
- Improve: a spike that uses a different network backend than the real lab (user-mode instead of
  TAP, so DHCP existed) hid D1; a unit test that asserts the *shape* of a script ("only del/flush")
  can lock in a bug (D4). Assert the outcome (the route exists) in a live test as well.
