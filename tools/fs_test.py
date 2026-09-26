# Persistence test: boot -> wizard -> unlock -> terminal shutdown (saves NVFS to hda)
# Then reboot with the same hda: must skip wizard and land on lock screen.
import socket, time, subprocess, os
QEMU = r"D:\qemu\qemu-system-x86_64.exe"
ISO = os.path.expandvars(r"%TEMP%\nefu_dist\nefuOS.iso")
IMG = r"D:\mycppos1\nefuOS\tools\nefu_test.img"
base = r"D:\mycppos1\nefuOS\tools\fs"
if not os.path.exists(IMG):
    with open(IMG, "wb") as f: f.write(b"\x00" * (64 * 1024 * 1024))
print("img ready", os.path.getsize(IMG))
for f in os.listdir(r"D:\mycppos1\nefuOS\tools"):
    if f.startswith("fs") and not f.endswith(".py"):
        try: os.remove(os.path.join(r"D:\mycppos1\nefuOS\tools", f))
        except Exception: pass

def start(port, stage):
    log = base + f"_{stage}_serial.log"
    dbg = base + f"_{stage}_dbg.log"
    p = subprocess.Popen([QEMU, "-machine", "pc,smm=off", "-cdrom", ISO,
        "-hda", IMG, "-boot", "d", "-display", "none",
        "-monitor", f"tcp:127.0.0.1:{port},server,nowait",
        "-no-reboot", "-m", "256M", "-vga", "std",
        "-device", "sb16", "-net", "nic,model=e1000", "-net", "user",
        "-debugcon", "file:" + dbg, "-serial", "file:" + log])
    return p, log

def cmd(s, c, wait=0.5):
    s.sendall((c + "\n").encode()); time.sleep(wait)
    try: return s.recv(65536).decode(errors="replace")
    except Exception: return ""

def keys(s, seq):
    for k in seq: cmd(s, "sendkey " + k, 0.12)

# ---- stage 1: first boot, setup, save ----
p1, log1 = start(4461, "one")
time.sleep(9)
s = socket.create_connection(("127.0.0.1", 4461), timeout=5)
keys(s, ["u","s","e","r","tab","u","s","e","r","tab","u","s","e","r","tab","ret"])
print("wizard done")
time.sleep(2.5)
keys(s, ["u","s","e","r","ret"])
print("unlock done")
time.sleep(2.5)
cmd(s, "sendkey f2", 0.8)           # open terminal
print("terminal opened")
time.sleep(1.2)
keys(s, ["s","h","u","t","d","o","w","n","ret"])   # type shutdown + enter
print("shutdown typed; waiting for guest poweroff...")
deadline = time.time() + 60
while time.time() < deadline and p1.poll() is None:
    time.sleep(1)
if p1.poll() is None:
    print("QEMU still running after 60s - kill")
    p1.kill()
else:
    print("stage1 qemu exited rc", p1.poll())
s.close()
time.sleep(1)

# ---- stage 2: reboot with same hda ----
p2, log2 = start(4462, "two")
time.sleep(9)   # enough for boot splash + (no wizard) lock screen
s = socket.create_connection(("127.0.0.1", 4462), timeout=5)
cmd(s, "screendump " + base + "_two_lock.bmp", 1.2)   # should be LOCK screen, not wizard
# unlock with the saved password
keys(s, ["u","s","e","r","ret"])
time.sleep(2.5)
cmd(s, "screendump " + base + "_two_desk.bmp", 1.2)
cmd(s, "stop", 1.0); time.sleep(0.3)
cmd(s, "screendump " + base + "_two_final.bmp", 1.2)
s.close()
print("stage2 done")
# keep qemu running for manual checks; report pids
open(os.path.expandvars(r"%TEMP%\fs_pids.txt"), "w").write(f"{p1.pid},{p2.pid}")
print("DONE")
