#!/usr/bin/env python3
"""Versioned image layer over the pinned golden CirrOS image (research R15).

  golden image (pinned file SHA-256, never written)
      <- layer cirros-<ver>.mvr<REV>.<logical8>.qcow2   thin qcow2, backing file = golden
          <- per-guest overlay                          throw-away (kvm.sh)

The layer carries the recipe in lab/kvm/image-patch/ (files that replace image content). The guest
OS lives in the initramfs, so the recipe is appended to /boot/initrd.img-* as one more cpio archive
(the kernel unpacks concatenated archives; later entries win) and written back into the ext4 root
partition with debugfs. Only the 64 KiB clusters that differ from the golden image are stored.

Trust anchors, all in lab/kvm/image.lock: the golden file SHA-256, MVR_PATCH_SHA256 (hash of the
recipe tree) and MVR_LAYER_LOGICAL_SHA256 (SHA-256 of what a guest reads: the layer flattened to
raw). The build is reproducible (own cpio writer, uncompressed, fixed times), so the logical hash
is a real pin. Standard library only; needs qemu-img, qemu-io and debugfs (e2fsprogs).

  image.py patch-hash --patch-dir DIR
  image.py build  --golden FILE --patch-dir DIR --out FILE      prints the logical SHA-256
  image.py ensure --lock FILE --cache DIR --patch-dir DIR       verify or build; prints the layer path
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

CLUSTER = 65536  # qcow2 default cluster size
FAKE_TIME = "1000000000"  # E2FSPROGS_FAKE_TIME: inode times in the written image
LINUX_FS = uuid.UUID("0FC63DAF-8483-4772-8E79-3D69D8477DE4")
PATH = os.environ.get("PATH", "") + ":/usr/sbin:/sbin"  # debugfs, e2fsck live in sbin on Debian


class ImageError(Exception):
    pass


def run(cmd, **kw):
    env = dict(os.environ, PATH=PATH, **kw.pop("env", {}))
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, env=env, **kw)
    if r.returncode != 0:
        raise ImageError(f"{cmd[0]} failed ({r.returncode}): {(r.stderr or r.stdout).strip()[:300]}")
    return r


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_lock(path):
    out = {}
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def layer_name(version, rev, logical_sha):
    return f"cirros-{version}.mvr{rev}.{logical_sha[:8]}.qcow2"


# ---- recipe -----------------------------------------------------------------------------------
def _files(patch_dir):
    root = Path(patch_dir)
    return sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(root).parts)


def tree_sha256(patch_dir):
    """Hash of the recipe: relative names, executable bit and content, independent of creation order."""
    root, h = Path(patch_dir), hashlib.sha256()
    for p in _files(root):
        data = p.read_bytes()
        x = "x" if os.access(p, os.X_OK) else "-"
        h.update(f"{p.relative_to(root).as_posix()}\0{x}\0{len(data)}\0".encode())
        h.update(data)
    return h.hexdigest()


def recipe_entries(patch_dir):
    """(name, mode, data) for every directory and file of the recipe, parents before children."""
    root = Path(patch_dir)
    dirs = set()
    for p in root.rglob("*"):
        if p.is_dir():
            dirs.add(p.relative_to(root).as_posix())
        elif p.is_file():
            parent = p.relative_to(root).parent
            while parent.parts:
                dirs.add(parent.as_posix())
                parent = parent.parent
    entries = [(d, 0o040755, b"") for d in dirs]
    for p in _files(root):
        mode = 0o100755 if os.access(p, os.X_OK) else 0o100644
        entries.append((p.relative_to(root).as_posix(), mode, p.read_bytes()))
    return sorted(entries, key=lambda e: tuple(e[0].split("/")))


def append_archive(initrd, archive):
    """initrd followed by `archive`, zero-padded so the archive starts on a 4-byte boundary (the
    kernel skips zero bytes between archives but rejects a misaligned uncompressed one)."""
    return initrd + b"\0" * (-len(initrd) % 4) + archive


def newc_archive(entries):
    """A reproducible `newc` cpio archive: inode numbers 1.., uid/gid 0, mtime 0, trailer included."""
    def header(ino, mode, nlink, size, name):
        raw = name.encode() + b"\0"
        fields = (ino, mode, 0, 0, nlink, 0, size, 0, 0, 0, 0, len(raw), 0)
        blob = b"070701" + b"".join(f"{v:08X}".encode() for v in fields) + raw
        return blob + b"\0" * (-len(blob) % 4)

    out = bytearray()
    for ino, (name, mode, data) in enumerate(entries, 1):
        nlink = 2 if mode & 0o170000 == 0o040000 else 1
        out += header(ino, mode, nlink, len(data), name)
        out += data + b"\0" * (-len(data) % 4)
    out += header(len(entries) + 1, 0, 1, 0, "TRAILER!!!")
    return bytes(out)


# ---- disk geometry ----------------------------------------------------------------------------
def gpt_linux_partition(path):
    """(first sector, sector count) of the one Linux-filesystem partition of a GPT disk image."""
    with open(path, "rb") as f:
        f.seek(512)
        hdr = f.read(92)
        if len(hdr) < 92 or hdr[:8] != b"EFI PART":
            raise ImageError("golden image has no GPT")
        entries_lba, n, size = struct.unpack_from("<QII", hdr, 72)[0], *struct.unpack_from("<II", hdr, 80)
        f.seek(entries_lba * 512)
        table = f.read(n * size)
    found = []
    for i in range(n):
        e = table[i * size:(i + 1) * size]
        if len(e) < 48 or not any(e[:16]):
            continue
        if uuid.UUID(bytes_le=e[:16]) == LINUX_FS:
            first, last = struct.unpack_from("<QQ", e, 32)
            found.append((first, last - first + 1))
    if len(found) != 1:
        raise ImageError(f"expected exactly one Linux filesystem partition, found {len(found)}")
    return found[0]


def differing_clusters(a, b, cluster=CLUSTER):
    """[(offset, length)] of the clusters whose bytes differ between two same-sized raw files."""
    out, off = [], 0
    with open(a, "rb") as fa, open(b, "rb") as fb:
        while True:
            x, y = fa.read(cluster), fb.read(cluster)
            if not x and not y:
                return out
            if x != y:
                out.append((off, len(y)))
            off += cluster


# ---- build ------------------------------------------------------------------------------------
def layer_logical_sha256(qcow2, workdir):
    flat = Path(workdir) / "flat.raw"
    try:
        run(["qemu-img", "convert", "-U", "-f", "qcow2", "-O", "raw", qcow2, flat])
        return sha256_file(flat)
    finally:
        flat.unlink(missing_ok=True)


def build_layer(golden, patch_dir, out, workdir):
    """Write the thin layer `out` (backing = golden) and return its logical SHA-256."""
    golden, out, wd = Path(golden).resolve(), Path(out), Path(workdir)
    entries = recipe_entries(patch_dir)
    if not entries:
        raise ImageError(f"empty recipe: {patch_dir}")
    gold_raw, root_img, full_raw = wd / "gold.raw", wd / "root.img", wd / "full.raw"
    run(["qemu-img", "convert", "-U", "-f", "qcow2", "-O", "raw", golden, gold_raw])
    start, count = gpt_linux_partition(gold_raw)
    with open(gold_raw, "rb") as f, open(root_img, "wb") as g:
        f.seek(start * 512)
        g.write(f.read(count * 512))

    listing = run(["debugfs", "-R", "ls -p /boot", root_img]).stdout
    names = [p.split("/")[5] for p in listing.splitlines() if p.count("/") >= 6]
    initrd = [n for n in names if n.startswith("initrd.img-")]
    if len(initrd) != 1:
        raise ImageError(f"expected one /boot/initrd.img-* in the image, found {initrd}")
    path = f"/boot/{initrd[0]}"
    orig, new = wd / "initrd.orig", wd / "initrd.new"
    orig.unlink(missing_ok=True)
    run(["debugfs", "-R", f"dump {path} {orig}", root_img])
    new.write_bytes(append_archive(orig.read_bytes(), newc_archive(entries)))
    os.chmod(new, 0o644)
    fake = {"E2FSPROGS_FAKE_TIME": FAKE_TIME}
    run(["debugfs", "-w", "-R", f"rm {path}", root_img], env=fake)
    run(["debugfs", "-w", "-R", f"write {new} {path}", root_img], env=fake)  # rc 0 even on error
    check = wd / "initrd.check"
    check.unlink(missing_ok=True)
    run(["debugfs", "-R", f"dump {path} {check}", root_img])
    if not check.exists() or check.read_bytes() != new.read_bytes():
        raise ImageError("debugfs did not write the patched initrd")
    run(["e2fsck", "-fn", root_img])

    shutil.copyfile(gold_raw, full_raw)
    with open(full_raw, "r+b") as f:
        f.seek(start * 512)
        f.write(root_img.read_bytes())
    clusters = differing_clusters(gold_raw, full_raw)
    expected = sha256_file(full_raw)

    out.unlink(missing_ok=True)
    run(["qemu-img", "create", "-q", "-f", "qcow2", "-b", golden, "-F", "qcow2", out])
    chunks = wd / "chunks"
    chunks.mkdir()
    cmds = []
    with open(full_raw, "rb") as f:
        for off, length in clusters:
            f.seek(off)
            p = chunks / str(off)
            p.write_bytes(f.read(length))
            cmds += ["-c", f"write -s {p} {off} {length}"]
    r = run(["qemu-io", "-f", "qcow2", *cmds, out])
    if "write failed" in r.stdout + r.stderr:
        raise ImageError("qemu-io could not write the layer")
    run(["qemu-img", "check", out])
    logical = layer_logical_sha256(out, wd)
    if logical != expected:
        raise ImageError("layer content differs from the patched image (internal error)")
    return logical


# ---- ensure -----------------------------------------------------------------------------------
def tool_versions():
    q = run(["qemu-img", "--version"]).stdout.splitlines()[0]
    d = subprocess.run(["debugfs", "-V"], capture_output=True, text=True,
                       env=dict(os.environ, PATH=PATH)).stderr.splitlines()
    return {"qemu-img": q, "debugfs": d[0] if d else "unknown"}


def _valid(layer, manifest, golden, lock, patch_sha, workdir):
    """True when the layer exists and every pin matches (checked before every bring-up)."""
    if not layer.exists() or not manifest.exists():
        return False
    try:
        m = json.loads(manifest.read_text())
        want = {"base_sha256": lock["CIRROS_SHA256"], "rev": int(lock["MVR_REV"]),
                "patch_sha256": patch_sha, "logical_sha256": lock["MVR_LAYER_LOGICAL_SHA256"]}
        if any(m.get(k) != v for k, v in want.items()):
            return False
        run(["qemu-img", "check", "-U", layer])  # -U: running guests have the layer open
        info = json.loads(run(["qemu-img", "info", "-U", "--output=json", layer]).stdout)
        if Path(info.get("full-backing-filename", "")).resolve() != golden.resolve():
            return False
        return layer_logical_sha256(layer, workdir) == lock["MVR_LAYER_LOGICAL_SHA256"]
    except (ImageError, ValueError, KeyError):
        return False


def ensure(lock_path, cache, patch_dir):
    lock, cache = read_lock(lock_path), Path(cache)
    golden = cache / lock["CIRROS_FILE"]
    if not golden.exists() or sha256_file(golden) != lock["CIRROS_SHA256"]:
        raise ImageError("golden image missing or checksum mismatch")
    patch_sha = tree_sha256(patch_dir)
    if patch_sha != lock["MVR_PATCH_SHA256"]:
        raise ImageError("recipe changed but image.lock was not updated "
                         "(bump MVR_REV, MVR_PATCH_SHA256 and MVR_LAYER_LOGICAL_SHA256)")
    layer = cache / layer_name(lock["CIRROS_VERSION"], lock["MVR_REV"], lock["MVR_LAYER_LOGICAL_SHA256"])
    manifest = layer.with_suffix(".qcow2.json")
    wd = Path(tempfile.mkdtemp(prefix=".work-", dir=cache))
    try:
        if _valid(layer, manifest, golden, lock, patch_sha, wd):
            return layer
        layer.unlink(missing_ok=True)
        manifest.unlink(missing_ok=True)
        part = layer.with_suffix(".qcow2.part")
        logical = build_layer(golden, patch_dir, part, wd)
        if logical != lock["MVR_LAYER_LOGICAL_SHA256"]:
            part.unlink(missing_ok=True)
            raise ImageError(f"build is not reproducible: logical SHA-256 is {logical}, "
                             f"image.lock pins {lock['MVR_LAYER_LOGICAL_SHA256']}")
        os.replace(part, layer)
        manifest.write_text(json.dumps({
            "layer": layer.name, "base": lock["CIRROS_FILE"], "base_sha256": lock["CIRROS_SHA256"],
            "rev": int(lock["MVR_REV"]), "patch_sha256": patch_sha, "logical_sha256": logical,
            "tools": tool_versions(), "built": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
            "size": layer.stat().st_size}, indent=1) + "\n")
        return layer
    finally:
        shutil.rmtree(wd, ignore_errors=True)


def main(argv=None):
    p = argparse.ArgumentParser(prog="image.py")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("patch-hash")
    s.add_argument("--patch-dir", required=True)
    s = sub.add_parser("build")
    s.add_argument("--golden", required=True)
    s.add_argument("--patch-dir", required=True)
    s.add_argument("--out", required=True)
    s = sub.add_parser("ensure")
    s.add_argument("--lock", required=True)
    s.add_argument("--cache", required=True)
    s.add_argument("--patch-dir", required=True)
    a = p.parse_args(argv)
    try:
        if a.cmd == "patch-hash":
            print(tree_sha256(a.patch_dir))
        elif a.cmd == "build":
            with tempfile.TemporaryDirectory(prefix="mvr-image-") as wd:
                print(build_layer(a.golden, a.patch_dir, a.out, wd))
        else:
            print(ensure(a.lock, a.cache, a.patch_dir))
    except ImageError as e:
        print(f"image.py: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
