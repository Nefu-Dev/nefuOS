import subprocess, sys, time, os

exe = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\huawei\AppData\Local\Temp\nefu_dist\nefuOS_dbg.exe"
url = sys.argv[2] if len(sys.argv) > 2 else "http://127.0.0.1:8123/mini600.html"
timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 25

args = [exe, "--app", "17", "--url", url]
outlog = os.path.join(os.path.dirname(os.path.abspath(__file__)), "host_crash_stdout.log")
print("RUN:", " ".join(args), flush=True)
p = subprocess.Popen(args, stdout=open(outlog, "wb"), stderr=subprocess.STDOUT)
t0 = time.time()
while time.time() - t0 < timeout:
    rc = p.poll()
    if rc is not None:
        print("EXITED rc=%d (0x%08X)" % (rc & 0xFFFFFFFF, rc & 0xFFFFFFFF), flush=True)
        print("CRASH_0xC0000374" if (rc & 0xFFFFFFFF) == 0xC0000374 else "NO_CRASH", flush=True)
        print("STDOUT ->", outlog, flush=True)
        sys.exit(0)
    time.sleep(0.5)
# still alive -> capped strings did not crash (window open)
p.kill()
print("ALIVE_AFTER_%ds -> NO_CRASH (window open, killed)" % int(timeout), flush=True)
