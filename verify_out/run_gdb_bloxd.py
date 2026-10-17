# -*- coding: utf-8 -*-
# Minimal gdb driver for nefuOS_dbg.exe with timeout + kill, writes bt to bloxd_gdb_bt.txt
import subprocess, sys, os, time, signal
GDB = r"D:\Dev-Cpp\MinGW64\bin\gdb.exe"
EXE = r"C:\Users\huawei\AppData\Local\Temp\nefu_dist\nefuOS_dbg.exe"
OUT = r"D:\mycppos1\nefuOS\verify_out\bloxd_gdb_bt.txt"
gdb_cmd = sys.argv[1] if len(sys.argv) > 1 else ""
timeout = int(sys.argv[2]) if len(sys.argv) > 2 else 50
cmd = [GDB, "-batch", "-x", gdb_cmd] if gdb_cmd else [GDB, "-batch", "-ex", "file %s" % EXE, "-ex", "run --app 17 --url http://127.0.0.1:8123/mini600.html", "-ex", "bt 30"]
with open(OUT, "w", encoding="utf-8", errors="replace") as f:
    p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=os.path.dirname(GDB))
try:
    p.wait(timeout=timeout)
except subprocess.TimeoutExpired:
    subprocess.run(["taskkill", "/F", "/IM", "nefuOS_dbg.exe"], capture_output=True)
    subprocess.run(["taskkill", "/F", "/IM", "gdb.exe"], capture_output=True)
    try: p.wait(timeout=5)
    except subprocess.TimeoutExpired: p.kill()
print("GDB_EXITED rc=%s t=%s" % (p.returncode, timeout))
