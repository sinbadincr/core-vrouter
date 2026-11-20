"""Unit tests for lab/kvm/image.py: the versioned image layer over the golden image (research R15).

Pure functions run anywhere. The end-to-end build runs only when the golden image is already in the
cache and qemu-img, qemu-io and debugfs exist; it is also the reproducibility proof: the layer must
read back with exactly the logical SHA-256 pinned in lab/kvm/image.lock.
"""
import hashlib
import importlib.util
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMAGE_PY = ROOT / "lab" / "kvm" / "image.py"
LOCK = ROOT / "lab" / "kvm" / "image.lock"
PATCH_DIR = ROOT / "lab" / "kvm" / "image-patch"

_spec = importlib.util.spec_from_file_location("mvr_kvm_image", IMAGE_PY)
image = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(image)

LINUX_FS = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4")
EFI_SYS = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")


def fake_gpt(entries, total_sectors=4096):
    """A minimal GPT disk: protective MBR sector, header at LBA 1, entries at LBA 2."""
    disk = bytearray(512 * total_sectors)
    hdr = bytearray(92)
    hdr[0:8] = b"EFI PART"
    struct.pack_into("<Q", hdr, 72, 2)            # first entry LBA
    struct.pack_into("<I", hdr, 80, 128)          # entries
    struct.pack_into("<I", hdr, 84, 128)          # entry size
    disk[512:512 + 92] = hdr
    for i, (guid, first, last) in enumerate(entries):
        e = bytearray(128)
        e[0:16] = guid.bytes_le
        struct.pack_into("<QQ", e, 32, first, last)
        disk[1024 + 128 * i:1024 + 128 * (i + 1)] = e
    return bytes(disk)


class RecipeHash(unittest.TestCase):
    def make(self, d, files):
        for name, data in files.items():
            p = Path(d) / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)

    def test_stable_and_content_sensitive(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            self.make(a, {"etc/x": b"1", "etc/y": b"2"})
            self.make(b, {"etc/y": b"2", "etc/x": b"1"})  # created in another order
            self.assertEqual(image.tree_sha256(a), image.tree_sha256(b))
            (Path(b) / "etc" / "x").write_bytes(b"changed")
            self.assertNotEqual(image.tree_sha256(a), image.tree_sha256(b))

    def test_sensitive_to_names_and_exec_bit(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            self.make(a, {"etc/x": b"1"})
            self.make(b, {"etc/z": b"1"})
            self.assertNotEqual(image.tree_sha256(a), image.tree_sha256(b))
            os.chmod(Path(a) / "etc" / "x", 0o755)
            self.make(b, {})
            shutil.rmtree(b)
            self.make(b, {"etc/x": b"1"})
            self.assertNotEqual(image.tree_sha256(a), image.tree_sha256(b))


class Cpio(unittest.TestCase):
    def parse(self, blob):
        """Walk a newc archive; returns [(name, mode, data)] without the trailer."""
        out, pos = [], 0
        while True:
            self.assertEqual(blob[pos:pos + 6], b"070701")
            f = [int(blob[pos + 6 + 8 * i:pos + 14 + 8 * i], 16) for i in range(13)]
            ino, mode, uid, gid, nlink, mtime, size, _, _, _, _, namesize, check = f
            name = blob[pos + 110:pos + 110 + namesize - 1].decode()
            self.assertEqual(blob[pos + 110 + namesize - 1], 0)
            pos = (pos + 110 + namesize + 3) & ~3
            data = blob[pos:pos + size]
            pos = (pos + size + 3) & ~3
            if name == "TRAILER!!!":
                return out, pos
            self.assertEqual((uid, gid, mtime, check), (0, 0, 0, 0))
            self.assertGreater(ino, 0)
            out.append((name, mode, data))

    def test_round_trip_alignment_and_trailer(self):
        entries = [("etc", 0o040755, b""), ("etc/init.d", 0o040755, b""),
                   ("etc/init.d/S40network", 0o100755, b"#!/bin/sh\nexit 0\n"),
                   ("etc/odd", 0o100644, b"abcde")]  # sizes not multiples of 4
        blob = image.newc_archive(entries)
        got, end = self.parse(blob)
        self.assertEqual(got, entries)
        self.assertEqual(end, len(blob))
        self.assertEqual(len(blob) % 4, 0)

    def test_deterministic(self):
        e = [("a", 0o100644, b"x")]
        self.assertEqual(image.newc_archive(e), image.newc_archive(list(e)))

    def test_recipe_entries_create_parents_first(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "etc" / "init.d"
            p.mkdir(parents=True)
            (p / "S40network").write_text("#!/bin/sh\n")
            os.chmod(p / "S40network", 0o755)
            ents = image.recipe_entries(d)
        self.assertEqual([e[0] for e in ents], ["etc", "etc/init.d", "etc/init.d/S40network"])
        self.assertEqual([oct(e[1]) for e in ents], ["0o40755", "0o40755", "0o100755"])


class AppendArchive(unittest.TestCase):
    """The kernel only unpacks an uncompressed cpio that starts on a 4-byte boundary; otherwise it
    logs "invalid magic at start of compressed archive" and silently drops the recipe."""

    def test_archive_starts_aligned_whatever_the_initrd_length(self):
        archive = image.newc_archive([("a", 0o100644, b"x")])
        for n in range(4, 12):
            orig = b"\x1f\x8b" + bytes(n)
            out = image.append_archive(orig, archive)
            start = len(out) - len(archive)
            self.assertEqual(start % 4, 0, n)
            self.assertEqual(out[start:], archive)
            self.assertTrue(out.startswith(orig))
            self.assertEqual(set(out[len(orig):start]), set() if start == len(orig) else {0})


class Gpt(unittest.TestCase):
    def test_finds_the_single_linux_filesystem_partition(self):
        disk = fake_gpt([(EFI_SYS, 100, 199), (LINUX_FS, 200, 999)])
        with tempfile.NamedTemporaryFile() as f:
            f.write(disk)
            f.flush()
            self.assertEqual(image.gpt_linux_partition(f.name), (200, 800))

    def test_refuses_zero_or_two(self):
        for entries in ([(EFI_SYS, 100, 199)], [(LINUX_FS, 100, 199), (LINUX_FS, 200, 299)]):
            with tempfile.NamedTemporaryFile() as f:
                f.write(fake_gpt(entries))
                f.flush()
                with self.assertRaises(image.ImageError):
                    image.gpt_linux_partition(f.name)

    def test_refuses_a_disk_without_gpt(self):
        with tempfile.NamedTemporaryFile() as f:
            f.write(bytes(8192))
            f.flush()
            with self.assertRaises(image.ImageError):
                image.gpt_linux_partition(f.name)


class Clusters(unittest.TestCase):
    def test_only_differing_clusters(self):
        cl = 4096
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            a.write_bytes(bytes(cl * 6))
            data = bytearray(cl * 6)
            data[cl * 1 + 5] = 1          # cluster 1
            data[cl * 4:cl * 5] = b"\xff" * cl  # cluster 4
            b.write_bytes(bytes(data))
            self.assertEqual(image.differing_clusters(a, b, cl), [(cl, cl), (4 * cl, cl)])

    def test_identical_and_short_tail(self):
        cl = 4096
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a", Path(d) / "b"
            a.write_bytes(bytes(cl * 2 + 100))
            b.write_bytes(bytes(cl * 2 + 100))
            self.assertEqual(image.differing_clusters(a, b, cl), [])
            data = bytearray(cl * 2 + 100)
            data[-1] = 9
            b.write_bytes(bytes(data))
            self.assertEqual(image.differing_clusters(a, b, cl), [(2 * cl, 100)])


class LockAndNames(unittest.TestCase):
    def test_layer_name(self):
        self.assertEqual(image.layer_name("0.6.3", 1, "ab" * 32), "cirros-0.6.3.mvr1.abababab.qcow2")

    def test_lock_pins_the_recipe_in_git(self):
        """Edit lab/kvm/image-patch/ without updating image.lock and `make check` fails here."""
        lock = image.read_lock(LOCK)
        self.assertRegex(lock["MVR_REV"], r"^[1-9][0-9]*$")
        self.assertRegex(lock["MVR_LAYER_LOGICAL_SHA256"], r"^[0-9a-f]{64}$")
        self.assertEqual(lock["MVR_PATCH_SHA256"], image.tree_sha256(PATCH_DIR),
                         "recipe changed: bump MVR_REV and update MVR_PATCH_SHA256 and the "
                         "layer's logical hash in lab/kvm/image.lock")

    def test_recipe_has_no_dhcp_client(self):
        script = (PATCH_DIR / "etc" / "init.d" / "S40network").read_text()
        self.assertNotIn("dhcpcd -w", script)
        self.assertTrue(os.access(PATCH_DIR / "etc" / "init.d" / "S40network", os.X_OK))


def _tools():
    return all(shutil.which(t, path=os.environ["PATH"] + ":/usr/sbin:/sbin")
               for t in ("qemu-img", "qemu-io", "debugfs"))


@unittest.skipUnless(_tools(), "qemu-img, qemu-io and debugfs are needed")
class EndToEnd(unittest.TestCase):
    def setUp(self):
        self.lock = image.read_lock(LOCK)
        cache = Path(os.environ.get("MINIVR_KVM_CACHE", ROOT / "build" / "cache" / "images"))
        self.golden = cache / self.lock["CIRROS_FILE"]
        if not self.golden.exists():
            self.skipTest("golden image not cached yet (run: sudo make lab once)")
        if hashlib.sha256(self.golden.read_bytes()).hexdigest() != self.lock["CIRROS_SHA256"]:
            self.skipTest("cached golden image does not match image.lock")

    def test_build_is_reproducible_and_golden_untouched(self):
        before = self.golden.stat().st_mtime_ns
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "layer.qcow2"
            logical = image.build_layer(self.golden, PATCH_DIR, out, Path(d))
            self.assertEqual(logical, self.lock["MVR_LAYER_LOGICAL_SHA256"])
            self.assertLess(out.stat().st_size, 20 * 1024 * 1024)  # thin: only changed clusters
            self.assertEqual(image.layer_logical_sha256(out, Path(d)), logical)
            info = subprocess.run(["qemu-img", "info", "--backing-chain", "--output=json", str(out)],
                                  capture_output=True, text=True, check=True).stdout
            self.assertIn(str(self.golden.resolve()), info)
        self.assertEqual(self.golden.stat().st_mtime_ns, before)


class Cli(unittest.TestCase):
    def test_patch_hash_matches_function(self):
        r = subprocess.run([sys.executable, str(IMAGE_PY), "patch-hash", "--patch-dir", str(PATCH_DIR)],
                           capture_output=True, text=True, check=True)
        self.assertEqual(r.stdout.strip(), image.tree_sha256(PATCH_DIR))


if __name__ == "__main__":
    unittest.main()
