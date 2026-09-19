# -*- coding: utf-8 -*-
"""Fetch and unpack the GRUB (x86_64-efi) + OVMF toolchain used to build the
ESP boot image.  Sources (official, pinned):
  - Debian pool main/g/grub2: grub-efi-amd64(-unsigned), grub-efi-amd64-bin,
    grub-common (GRUB 2.14) -> monolithic grubx64.efi + x86_64-efi/*.mod
  - Arch archive: edk2-ovmf 202608 -> OVMF_CODE.4m.fd / OVMF_VARS.4m.fd

Outputs:
  tools/grub_toolchain/extracted/usr/lib/grub/x86_64-efi/   (grubx64.efi, modules)
  tools/grub_toolchain/ovmf/                                (OVMF*.fd)

Idempotent: existing files are skipped.
"""
import io
import os
import subprocess
import sys
import tarfile
import urllib.request

TOOLCHAIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "grub_toolchain")
DEB_BASE = "https://deb.debian.org/debian/pool/main/g/grub2/"
DEBS = [
    "grub-efi-amd64-bin_2.14-3_amd64.deb",
    "grub-efi-amd64-unsigned_2.14-3_amd64.deb",
    "grub-efi-amd64_2.14-3_amd64.deb",
    "grub-common_2.14-3_amd64.deb",
]
OVMF_URL = "https://archive.archlinux.org/packages/e/edk2-ovmf/edk2-ovmf-202608-1-any.pkg.tar.zst"
OVMF_PKG = os.path.join(TOOLCHAIN, "edk2-ovmf-202608-1-any.pkg.tar.zst")


def fetch(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        print("have %s" % os.path.basename(dest))
        return
    print("downloading %s ..." % url)
    req = urllib.request.Request(url, headers={"User-Agent": "nefuOS-build/1.0"})
    with urllib.request.urlopen(req, timeout=600) as r, open(dest, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    print("  -> %s (%d bytes)" % (dest, os.path.getsize(dest)))


def extract_deb(deb, dest):
    data = open(deb, "rb").read()
    if data[:8] != b"!<arch>\n":
        raise RuntimeError("not an ar archive: " + deb)
    pos = 8
    while pos + 60 <= len(data):
        name = data[pos:pos + 16].decode("ascii", "replace").strip()
        size = int(data[pos + 48:pos + 58].decode("ascii", "replace").strip() or b"0")
        pos += 60
        body = data[pos:pos + size]
        pos += size + (size & 1)
        if name.startswith("data.tar"):
            tf = tarfile.open(fileobj=io.BytesIO(body), mode="r:xz")
            tf.extractall(dest)
            print("extracted %s" % os.path.basename(deb))
            return


def extract_ovmf():
    out = os.path.join(TOOLCHAIN, "ovmf")
    os.makedirs(out, exist_ok=True)
    if os.listdir(out):
        print("ovmf already extracted")
        return
    try:
        import zstandard
    except ImportError:
        print("installing zstandard ...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "--quiet", "zstandard"])
        import zstandard  # noqa
    dctx = zstandard.ZstdDecompressor()
    with open(OVMF_PKG, "rb") as f:
        raw = dctx.stream_reader(f)
        tf = tarfile.open(fileobj=raw, mode="r|")
        for m in tf:
            if m.isfile() and m.name.endswith(".fd"):
                with open(os.path.join(out, os.path.basename(m.name)), "wb") as o, tf.extractfile(m) as srcf:
                    o.write(srcf.read())
    print("ovmf extracted -> %s" % out)


def main():
    os.makedirs(TOOLCHAIN, exist_ok=True)
    for d in DEBS:
        fetch(DEB_BASE + d, os.path.join(TOOLCHAIN, d))
    fetch(OVMF_URL, OVMF_PKG)

    dest = os.path.join(TOOLCHAIN, "extracted")
    os.makedirs(dest, exist_ok=True)
    for d in DEBS:
        extract_deb(os.path.join(TOOLCHAIN, d), dest)
    extract_ovmf()

    core = os.path.join(dest, "usr", "lib", "grub", "x86_64-efi", "monolithic", "grubx64.efi")
    mods = os.path.join(dest, "usr", "lib", "grub", "x86_64-efi")
    if not os.path.exists(core):
        raise RuntimeError("grubx64.efi missing after extraction")
    print("OK: grubx64.efi = %s" % core)
    print("OK: modules = %s (%d .mod files)" % (mods, len([f for f in os.listdir(mods) if f.endswith('.mod')])))
    print("OK: ovmf = %s" % os.path.join(TOOLCHAIN, "ovmf"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
