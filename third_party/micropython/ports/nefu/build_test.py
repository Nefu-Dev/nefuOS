# build_test.py — compile py core + nefu port and link a tiny embed test.
import os, sys, glob, subprocess, shutil

ROOT   = r"D:\mycppos1\nefuOS\third_party\micropython"
PORT   = os.path.join(ROOT, "ports", "nefu")
GENHDR = os.path.join(PORT, "genhdr")
GCC    = r"D:\CLion\bin\mingw\bin\gcc.exe"
OUT    = os.path.join(os.environ["TEMP"], "nefu_mp_test")

shutil.rmtree(OUT, ignore_errors=True)
os.makedirs(OUT, exist_ok=True)

CFLAGS = ["-O2", "-g", "-w", "-std=gnu11", "-D_GNU_SOURCE",
          "-I", ROOT, "-I", PORT, "-I", GENHDR]

srcs = sorted(glob.glob(os.path.join(ROOT, "py", "*.c")))
srcs.append(os.path.join(PORT, "nefu_mp.c"))
srcs.append(os.path.join(PORT, "modnefu_http.c"))
srcs.append(os.path.join(ROOT, "shared", "runtime", "gchelper_generic.c"))
# extmod stdlib modules (must match the set in gen_headers.py)
extmod = [
    "modasyncio.c", "modbinascii.c", "moddeflate.c",
    "modframebuf.c", "modhashlib.c", "modheapq.c", "modjson.c",
    "modplatform.c", "modrandom.c", "modre.c", "modselect.c",
    "modtime.c", "moductypes.c",
]
srcs += [os.path.join(ROOT, "extmod", f) for f in extmod]
srcs.append(os.path.join(PORT, "frozen_content.c"))
srcs.append(os.path.join(PORT, "test_embed.c"))

objs = []
for s in srcs:
    name = os.path.splitext(os.path.basename(s))[0]
    o = os.path.join(OUT, name + ".o")
    r = subprocess.run([GCC] + CFLAGS + ["-c", s, "-o", o], capture_output=True, text=True)
    if r.returncode != 0:
        print("COMPILE FAIL:", s)
        print(r.stderr[:4000])
        sys.exit(1)
    objs.append(o)

exe = os.path.join(OUT, "test_micropython.exe")
r = subprocess.run([GCC, "-o", exe, "-Wl,--disable-dynamicbase", "-Wl,--image-base,0x140000000"] + objs + [r"D:\CLion\bin\mingw\x86_64-w64-mingw32\lib\libdbghelp.a"], capture_output=True, text=True)
if r.returncode != 0:
    print("LINK FAIL")
    print(r.stderr[:8000])
    sys.exit(1)
print("LINK OK:", exe, os.path.getsize(exe), "bytes")
