# Third-party components

Test-only: none of these is linked into or shipped with `minivrd` or `vrctl`.

| Component | Version | License | Source | Use |
|---|---|---|---|---|
| pytest | 8.3.4 | MIT | https://github.com/pytest-dev/pytest | e2e test runner |
| scapy | 2.6.1 | GPL-2.0-only | https://github.com/secdev/scapy | packet crafting in e2e tests |

## Guest images (lab only, never linked or shipped)

Downloaded on first use of `make lab` into `build/cache/images/` and verified by SHA-256
(`lab/kvm/image.lock`); never committed to the repository.

| Component | Version | License | Source | SHA-256 | Use |
|---|---|---|---|---|---|
| CirrOS | 0.6.3 (x86_64 disk) | GPL-2.0 | https://download.cirros-cloud.net/0.6.3/cirros-0.6.3-x86_64-disk.img | `7d6355852aeb6dbcd191bcda7cd74f1536cfe5cbf8a10495a7283a8396e4b75b` | KVM guests vm1–vm3 in the KVM lab (the default lab) |

The lab also builds one **derived layer** on top of the CirrOS image (`lab/kvm/image.py`): a qcow2
file that stores only the clusters changed by `lab/kvm/image-patch/` (a replacement init script
without the DHCP wait). It stays in the local cache, is never committed or distributed, and is
pinned by the hash of its recipe and of its logical content in `lab/kvm/image.lock`.
