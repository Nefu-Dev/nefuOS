#!/usr/bin/env python3
"""freeze_asyncio.py - generate frozen_content.c (MICROPY_MODULE_FROZEN_STR)
for the asyncio package, matching MicroPython's generate_frozen_str_content().

The asyncio package is frozen as strings so `import asyncio` works on the
host and bare-metal builds.  Order matters: the package __init__ must come
first so mp_find_frozen_module() resolves `asyncio` as a file (not a dir).
"""
import os

ROOT = r"D:\mycppos1\nefuOS\third_party\micropython"
ASYNCIO = os.path.join(ROOT, "extmod", "asyncio")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frozen_content.c")

# (full_path, target_name) - order is significant
# The __init__.py of a package must be named "<pkg>/__init__.py" so that
# `import <pkg>` resolves to a DIRECTORY (package) and the import machinery
# loads <pkg>/__init__.py and sets __path__.
MODULES = [
    (os.path.join(ASYNCIO, "__init__.py"), "asyncio/__init__.py"),
    (os.path.join(ASYNCIO, "core.py"), "asyncio/core.py"),
    (os.path.join(ASYNCIO, "event.py"), "asyncio/event.py"),
    (os.path.join(ASYNCIO, "funcs.py"), "asyncio/funcs.py"),
    (os.path.join(ASYNCIO, "lock.py"), "asyncio/lock.py"),
    (os.path.join(ASYNCIO, "stream.py"), "asyncio/stream.py"),
    (os.path.join(ASYNCIO, "uasyncio.py"), "uasyncio.py"),
]


def gen_str_content():
    out = [b"#include <stdint.h>\n", b"#define MP_FROZEN_STR_NAMES \\\n"]
    for _, target in MODULES:
        out.append(b'"%s\\0" \\\n' % target.encode())
    out.append(b"\n")
    out.append(b"const uint32_t mp_frozen_str_sizes[] = { ")
    for full_path, _ in MODULES:
        out.append(b"%d, " % os.stat(full_path).st_size)
    out.append(b"0 };\n")
    out.append(b"const char mp_frozen_str_content[] = {\n")
    esc = {ord("\n"): b"\\n", ord("\r"): b"\\r", ord('"'): b'\\"', ord("\\"): b"\\\\"}
    for full_path, _ in MODULES:
        with open(full_path, "rb") as f:
            data = bytearray(f.read())
        out.append(b'"')
        break_str = False
        for c in data:
            if c in esc:
                out.append(esc[c])
            elif 32 <= c <= 126:
                if break_str:
                    out.append(b'" "')
                    break_str = False
                out.append(chr(c).encode())
            else:
                out.append(b"\\x%02x" % c)
                break_str = True
        out.append(b'\\0"\n')
    out.append(b'"\\0"\n};\n\n')
    out.append(b'const char mp_frozen_names[] = { MP_FROZEN_STR_NAMES "\\0"};\n')
    return b"".join(out)


def main():
    data = gen_str_content()
    with open(OUT, "wb") as f:
        f.write(data)
    print("wrote", OUT, len(data), "bytes")


if __name__ == "__main__":
    main()
