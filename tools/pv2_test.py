# Persistence test v3: verify each step with screenshots
import socket, time, subprocess, os
QEMU = r"D:\qemu\qemu-system-x86_64.exe"
ISO = os.path.expandvars(r"%TEMP%\nefu_dist\nefuOS.iso")
IMG = r"D:\mycppos1\nefuOS\tools\nefu_test.img"
base = r"D:\mycppos1\nefuOS\tools\pv2"
if not os.path.exists(IMG):
    with open(IMG, "wb") as f: f.write(b"\x00" * (64 * 1024 * 1024))
for f in os.listdir(r"D:\mycppos1\nefuOS\tools"):
    if f.startswith("pv2") and not f.endswith(".py"):
        try: os.remove(os.path.join(r"D:\mycppos1\nefuOS\tools", f))
        except Exception: pass

def start(port, stage):
    log = base + f"_{stage}_serial.log"; dbg = base + f"_{stage}_dbg.log"
    p = subprocess.Popen([QEMU, "-machine", "pc,smm=off", "-cdrom", ISO,
        "-drive", f"file={IMG},format=raw,if=ide,index=0", "-boot", "d",
        "-display", "none", "-monitor", f"tcp:127.0.0.1:{port},server,nowait",
        "-no-reboot", "-m", "256M", "-vga", "std",
        "-device", "sb16", "-net", "nic,model=e1000", "-net", "user",
        "-debugcon", "file:" + dbg, "-serial", "file:" + log])
    return p

def cmd(s, c, wait=0.5):
    s.sendall((c + "\n").encode()); time.sleep(wait)
    try: return s.recv(65536).decode(errors="replace")
    except Exception: return ""

def keys(s, seq):
    for k in seq: cmd(s, "sendkey " + k, 0.12)

p1 = start(4468, "one")
time.sleep(9)
s = socket.create_connection(("127.0.0.1", 4468), timeout=5)
keys(s, ["u","s","e","r","tab","u","s","e","r","tab","u","s","e","r","tab","ret"])
print("wizard done"); time.sleep(2.5)
cmd(s, "screendump " + base + "_lock1.bmp", 1.2)     # expect lock screen
keys(s, ["u","s","e","r","ret"])
print("unlock sent"); time.sleep(2.5)
cmd(s, "screendump " + base + "_desk.bmp", 1.2)      # expect desktop
cmd(s, "sendkey f2", 0.8)
print("f2 sent"); time.sleep(1.5)
cmd(s, "screendump " + base + "_term.bmp", 1.2)      # expect terminal
keys(s, ["r","e","b","o","o","t","ret"])
print("reboot typed; waiting for exit...")
deadline = time.time() + 60
while time.time() < deadline and p1.poll() is None:
    time.sleep(1)
if p1.poll() is None:
    print("QEMU still running - kill")
    p1.kill()
else:
    print("stage1 exited rc", p1.poll())
s.close()
print("DONE")
