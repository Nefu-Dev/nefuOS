import subprocess, sys, time, os

exe = r"C:\Users\huawei\AppData\Local\Temp\nefu_dist\nefuOS_dbg.exe"
def run(url, timeout=25):
    outlog = os.path.join(os.path.dirname(os.path.abspath(__file__)), "host_crash_stdout.log")
    p = subprocess.Popen([exe, "--app", "17", "--url", url], stdout=open(outlog, "wb"), stderr=subprocess.STDOUT)
    t0 = time.time()
    while time.time() - t0 < timeout:
        rc = p.poll()
        if rc is not None:
            return ("CRASH", rc & 0xFFFFFFFF, outlog)
        time.sleep(0.5)
    p.kill()
    return ("ALIVE", 0, outlog)

tests = [
    "http://127.0.0.1:8123/mini600.html",
    "http://127.0.0.1:8123/mini600.html",
    "http://127.0.0.1:8123/bigtext_850k.html",
]
for u in tests:
    st, rc, lg = run(u)
    tag = "CRASH_0xC0000374" if (rc == 0xC0000374) else ("OK" if st == "ALIVE" else "rc=%d"%rc)
    print("%-45s -> %s (rc=0x%X)" % (u.split("/")[-1], tag, rc), flush=True)
