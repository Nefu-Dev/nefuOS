# gen_headers.py — generate MicroPython's required headers for the nefu port
# Replicates the make-driven steps in py/mkrules.mk + py/py.mk for a no-make build.
# Output headers land in ports/nefu/genhdr/.

import os, sys, glob, subprocess, tempfile, shutil

ROOT   = r"D:\mycppos1\nefuOS\third_party\micropython"
PORT   = os.path.join(ROOT, "ports", "nefu")
GENHDR = os.path.join(PORT, "genhdr")
GCC    = r"D:\CLion\bin\mingw\bin\gcc.exe"
PY     = r"C:\Users\huawei\AppData\Local\Programs\Python\Python311\python.exe"

if os.path.isdir(GENHDR):
    shutil.rmtree(GENHDR)
os.makedirs(GENHDR, exist_ok=True)

srcs = sorted(glob.glob(os.path.join(ROOT, "py", "*.c")))
srcs.append(os.path.join(PORT, "nefu_mp.c"))
srcs.append(os.path.join(PORT, "modnefu_http.c"))  # nefu_http HTTPS bridge module
# extmod stdlib modules compiled into the build: their MP_REGISTER_MODULE
# registrations must be visible to makemoduledefs, so preprocess them too.
extmod = [
    "modasyncio.c", "modbinascii.c", "moddeflate.c",
    "modframebuf.c", "modhashlib.c", "modheapq.c", "modjson.c",
    "modplatform.c", "modrandom.c", "modre.c", "modselect.c",
    "modtime.c", "moductypes.c",
]
srcs += [os.path.join(ROOT, "extmod", f) for f in extmod]

inc = ["-I", ROOT, "-I", PORT, "-I", GENHDR]

def check(r, what):
    if r.returncode != 0:
        sys.stderr.write(r.stdout or "")
        sys.stderr.write(r.stderr or "")
        raise SystemExit("FAILED: " + what)

# ---- 1. mpversion.h must exist before preprocessing/compiling ----
r = subprocess.run([PY, os.path.join(ROOT, "py", "makeversionhdr.py"),
                    os.path.join(GENHDR, "mpversion.h")], capture_output=True, text=True)
check(r, "makeversionhdr")

# ---- 2. preprocess all sources into qstr.i.last (plain -E: line markers) ----
qstr_i = os.path.join(GENHDR, "qstr.i.last")
r = subprocess.run([GCC, "-E", "-DNO_QSTR"] + inc + srcs, capture_output=True, text=True, errors="replace")
check(r, "preprocess")
with open(qstr_i, "w", encoding="utf-8", errors="replace") as f:
    f.write(r.stdout)
print("qstr.i.last written, %d bytes" % os.path.getsize(qstr_i))

work = tempfile.mkdtemp(prefix="mpy_hdr_")

# ---- 3. split / cat qstr, module, root_pointer ----
collect = {
    "qstr": os.path.join(GENHDR, "qstrdefs.collected.h"),
    "module": os.path.join(GENHDR, "moduledefs.collected"),
    "root_pointer": os.path.join(GENHDR, "root_pointers.collected"),
}
for mode, out in collect.items():
    r = subprocess.run([PY, os.path.join(ROOT, "py", "makeqstrdefs.py"),
                        "split", mode, qstr_i, work, "_"], capture_output=True, text=True)
    check(r, "split " + mode)
    r = subprocess.run([PY, os.path.join(ROOT, "py", "makeqstrdefs.py"),
                        "cat", mode, "_", work, out], capture_output=True, text=True)
    check(r, "cat " + mode)
print("collected files:", [os.path.getsize(p) for p in collect.values()])

# ---- 4. qstrdefs.generated.h ----
concat = []
for p in [os.path.join(ROOT, "py", "qstrdefs.h"),
          os.path.join(PORT, "qstrdefsport.h"),
          collect["qstr"]]:
    with open(p, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.rstrip("\n")
            if s.startswith("Q("):
                concat.append('"%s"' % s)
            else:
                concat.append(s)
# feed through gcc -E, then unwrap "Q(...)" back to Q(...) (the wrap protects
# the macro from expansion inside string literals; the unwrap restores it)
r = subprocess.run([GCC, "-E", "-DNO_QSTR"] + ["-I", ROOT, "-I", PORT, "-I", GENHDR, "-"],
                   input="\n".join(concat), capture_output=True, text=True, errors="replace")
check(r, "qstr preprocess")
unwrapped = []
for line in r.stdout.splitlines():
    s = line.rstrip("\n")
    if s.startswith('"Q(') and s.endswith(')"'):
        s = s[1:-1]  # strip surrounding quotes: "Q(x)" -> Q(x)
    unwrapped.append(s)
pre = os.path.join(work, "qstrdefs.preprocessed.h")
with open(pre, "w", encoding="utf-8", errors="replace") as f:
    f.write("\n".join(unwrapped))
r = subprocess.run([PY, os.path.join(ROOT, "py", "makeqstrdata.py"), pre],
                   capture_output=True, text=True, errors="replace")
check(r, "makeqstrdata")
with open(os.path.join(GENHDR, "qstrdefs.generated.h"), "w", encoding="utf-8") as f:
    f.write(r.stdout)
print("qstrdefs.generated.h: %d bytes" % os.path.getsize(os.path.join(GENHDR, "qstrdefs.generated.h")))

# ---- 5. moduledefs.h ----
r = subprocess.run([PY, os.path.join(ROOT, "py", "makemoduledefs.py"), collect["module"]],
                   capture_output=True, text=True, errors="replace")
check(r, "makemoduledefs")
with open(os.path.join(GENHDR, "moduledefs.h"), "w", encoding="utf-8") as f:
    f.write(r.stdout)
print("moduledefs.h: %d bytes" % os.path.getsize(os.path.join(GENHDR, "moduledefs.h")))

# ---- 6. root_pointers.h ----
r = subprocess.run([PY, os.path.join(ROOT, "py", "make_root_pointers.py"), collect["root_pointer"]],
                   capture_output=True, text=True, errors="replace")
check(r, "make_root_pointers")
with open(os.path.join(GENHDR, "root_pointers.h"), "w", encoding="utf-8") as f:
    f.write(r.stdout)
print("root_pointers.h: %d bytes" % os.path.getsize(os.path.join(GENHDR, "root_pointers.h")))

shutil.rmtree(work, ignore_errors=True)
print("DONE. Headers in:", GENHDR)
