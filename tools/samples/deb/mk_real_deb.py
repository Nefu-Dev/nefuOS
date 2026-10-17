import os, tarfile, io, time

d = "tools/samples/deb/hello-deb"

def tarball(src_base, names):
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode="w:gz") as t:
        for n in names:
            p = os.path.join(src_base, n)
            ti = tarfile.TarInfo(name=n)
            ti.size = os.path.getsize(p)
            ti.mtime = int(time.time())
            ti.mode = 0o755 if n == "usr/bin/hello" else 0o644
            with open(p, "rb") as f:
                t.addfile(ti, f)
    return b.getvalue()

ctl_names = ["debian-binary", "DEBIAN/control"]
data_names = []
for r, _, fs in os.walk(os.path.join(d, "data")):
    for fn in fs:
        full = os.path.join(r, fn)
        rel = os.path.relpath(full, os.path.join(d, "data")).replace(os.sep, "/")
        data_names.append(rel)  # data.tar members are rooted at "/"

ctar = tarball(d, ctl_names)
dtar = tarball(os.path.join(d, "data"), data_names)

def ar_member(name, payload):
    # ar member header is exactly 60 bytes
    name_b = name.encode("ascii")[:16]
    name_b = name_b + b" " * (16 - len(name_b))
    mtime = b"0".ljust(12, b" ")
    uid = b"0".ljust(6, b" ")
    gid = b"0".ljust(6, b" ")
    mode = b"100644".ljust(8, b" ")
    size = str(len(payload)).encode("ascii").ljust(10, b" ")
    magic = b"`\n"
    header = name_b + mtime + uid + gid + mode + size + magic
    assert len(header) == 60, len(header)
    pad = b"\n" * ((2 - (len(payload) & 1)) & 1)
    return header + payload + pad

out = b"!<arch>\n"
out += ar_member("debian-binary", b"2.0\n")
out += ar_member("control.tar.gz/", ctar)
out += ar_member("data.tar.gz/", dtar)

with open("tools/samples/deb/real.deb", "wb") as f:
    f.write(out)
print("real.deb bytes:", len(out))
