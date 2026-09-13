#!/usr/bin/env python3
# nefuOS admin password hashing (build-time, inject-only).
# Reads NEFU_ADMIN_PASSWORD (argv[1]), computes salted SHA-256 (hex),
# writes core/sys/admin_hash.h. The plain password is consumed and never
# stored.  Bare-metal verifies with core/sys/sha256.cpp (same algorithm).
import sys, os, hashlib, random

SALT = b"nefuos-admin-salt-v1"

def salted_sha256(s: str) -> str:
    return hashlib.sha256(SALT + s.encode("utf-8")).hexdigest()

pw = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else None
if pw:
    h = salted_sha256(pw)
    note = "admin hash written (password consumed, not stored)"
else:
    h = hashlib.sha256(SALT + random.randbytes(16)).hexdigest()
    note = "no NEFU_ADMIN_PASSWORD: random placeholder hash (login denied)"
print(note)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "core", "sys", "admin_hash.h")
with open(out, "w", encoding="ascii") as f:
    f.write("// auto-generated at build time; contains ONLY a salted SHA-256 hash\n")
    f.write("#define NEFU_ADMIN_HASH \"%s\"\n" % h)
