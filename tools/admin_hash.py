#!/usr/bin/env python3
# nefuOS admin password hashing (build-time, inject-only).
# Reads NEFU_ADMIN_PASSWORD (argv[1]), computes FNV-1a 64 (hex), writes
# core/sys/admin_hash.h. The plain password is consumed and never stored.
import sys, os, random

def fnv1a64(s: str) -> int:
    h = 14695981039346656037
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h

pw = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
if pw:
    h = fnv1a64(pw)
    note = "admin hash written (password consumed, not stored)"
else:
    h = random.getrandbits(64)
    note = "no NEFU_ADMIN_PASSWORD: random placeholder hash (login denied)"
print(note)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core", "sys", "admin_hash.h")
with open(out, "w", encoding="ascii") as f:
    f.write("// auto-generated at build time; contains ONLY a password hash\n")
    f.write("#define NEFU_ADMIN_HASH \"%016x\"\n" % h)
