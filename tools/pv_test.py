# Persistence test v2: wizard -> unlock -> terminal reboot (saves NVFS to hda)
# Then reboot with same hda: must skip wizard and land on lock screen.
import socket, time, subprocess, os
QEMU = r"D:\qemu\qemu-system-x86_64.exe"
ISO = os.path.expandvars(r"%TEMP%\nefu_dist\nefuOS.iso")
IMG = r"D:\mycppos1\nefuOS\tools\nefu_test.img"
base = r"D:\mycppos1\nefuOS\tools\pv"
if not os.path.exists(IMG):
    with open(IMG, "wb") as f: f.write(b"\x00" * (64 * 1024 * 1024))
for f in os.listdir(r"D:\mycppos1\nefuOS\tools"):
    if f.startswith("pv") and not f.endswith(".py"):
        try: os.remove(os.path.join(r"D:\mycppos1\nefuOS\tools", f))
        except Exception: pass

def start(port, stage):
    log = base + f"_{stage}_serial.log"
    dbg = base + f"_{stage}_dbg.log"
    p = subprocess.Popen([QEMU, "-machine", "pc,smm=off", "-cdrom", ISO,
        "-drive", f"file={IMG},format=raw,if=ide,index=0", "-boot", "d",
        "-display", "none", "-monitor", f"tcp:127.0.0.1:{port},server,nowait",
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

# ---- stage 1: first boot, setup, save via reboot ----
p1, log1 = start(4466, "one")
time.sleep(9)
s = socket.create_connection(("127.0.0.1", 4466), timeout=5)
keys(s, ["u","s","e","r","tab","u","s","e","r","tab","u","s","e","r","tab","ret"])
print("wizard done"); time.sleep(2.5)
keys(s, ["u","s","e","r","ret"])
print("unlock done"); time.sleep(2.5)
cmd(s, "sendkey f2", 0.8); print("terminal"); time.sleep(1.2)
keys(s, ["r","e","b","o","o","t","ret"])
print("reboot typed; waiting for guest reset / qemu exit...")
deadline = time.time() + 60
while time.time() < deadline and p1.poll() is None:
    time.sleep(1)
if p1.poll() is None:
    print("QEMU still running - kill")
    p1.kill()
else:
    print("stage1 qemu exited rc", p1.poll())
s.close()
time.sleep(1)

# ---- stage 2: reboot with same hda ----
p2, log2 = start(4467, "two")
time.sleep(9)
s = socket.create_connection(("127.0.0.1", 4467), timeout=5)
cmd(s, "screendump " + base + "_two_lock.bmp", 1.2)   # expect LOCK screen, not wizard
keys(s, ["u","s","e","r","ret"])
time.sleep(2.5)
cmd(s, "screendump " + base + "_two_desk.bmp", 1.2)
cmd(s, "stop", 1.0); time.sleep(0.3)
cmd(s, "screendump " + base + "_two_final.bmp", 1.2)
s.close()
print("stage2 done")
open(os.path.expandvars(r"%TEMP%\pv_pids.txt"), "w").write(f"{p1.pid},{p2.pid}")
print("DONE")
