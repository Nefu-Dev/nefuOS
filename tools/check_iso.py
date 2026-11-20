"""Structural check of the built ISO: boot sector fields vs the bare payload.

Used when a QEMU run is impossible (host commit exhausted). It cannot prove the
kernel runs, but it does prove the ISO carries the exact payload the linker
produced and that the boot sector's kernel counter matches it.
"""
import hashlib
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
iso = root / "build" / "iso" / "nefuOS.iso"
payload = root / "build" / "bare" / "payload.bin"
boot = root / "build" / "bare" / "boot.bin"
kernel = root / "build" / "bare" / "kernel.bin"

SECTOR = 2048
problems = []
lines = []


def ok(cond, msg):
    lines.append(("OK   " if cond else "FAIL ") + msg)
    if not cond:
        problems.append(msg)


for p in (iso, payload, boot, kernel):
    ok(p.exists(), f"exists: {p.name} ({p.stat().st_size} B)" if p.exists() else f"missing: {p}")

iso_bytes = iso.read_bytes()
pay = payload.read_bytes()
boot_bytes = boot.read_bytes()

ok(len(boot_bytes) == 512, f"boot.bin is 512 bytes (got {len(boot_bytes)})")
ok(boot_bytes[510] == 0x55 and boot_bytes[511] == 0xAA,
   "boot.bin has the 0x55AA signature")

count = struct.unpack_from("<I", boot_bytes, 0x58)[0]
# boot.s stores the payload length in 512-byte units and divides by 4 to get
# CD sectors, so the counter has to cover payload.bin exactly
lines.append(f"INFO boot.bin counter at 0x58 = {count} (x512 = {count * 512} B); payload {len(pay)} B")
ok((count * 512) >= len(pay), "boot counter covers the payload")
ok(len(iso_bytes) >= (26 + 2048) * SECTOR, "ISO reserves the full 2048-block kernel area")

# make_iso.py writes the payload verbatim at LBA 26 (see tools/make_iso.py)
lba_kernel = 26 * SECTOR
in_iso = iso_bytes[lba_kernel:lba_kernel + len(pay)]
ok(in_iso == pay, "ISO payload at LBA26 is byte-identical to build/bare/payload.bin")

lines.append(f"INFO payload sha256 {hashlib.sha256(pay).hexdigest()[:16]}")
lines.append(f"INFO kernel  sha256 {hashlib.sha256(kernel.read_bytes()).hexdigest()[:16]}")
lines.append(f"INFO iso sectors {len(iso_bytes) // SECTOR}")

print("\n".join(lines))
sys.exit(1 if problems else 0)
