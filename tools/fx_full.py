# Full lock/unlock/relock/unlock validation on the fixed ISO
import socket, time, subprocess, os, sys
QEMU = r"D:\qemu\qemu-system-x86_64.exe"
ISO = os.path.expandvars(r"%TEMP%\nefu_dist\nefuOS.iso")
PORT = 4460
base = r"D:\mycppos1\nefuOS\tools\fx"
log = base + "_serial.log"
dbg = base + "_dbg.log"
# remove stale evidence
for f in os.listdir(r"D:\mycppos1\nefuOS\tools"):
    if f.startswith("fx") and not f.endswith(".py"):
        try: os.remove(os.path.join(r"D:\mycppos1\nefuOS\tools", f))
        except Exception: pass
proc = subprocess.Popen([QEMU, "-machine", "pc,smm=off", "-cdrom", ISO,
    "-boot", "d", "-display", "none",
    "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait",
    "-no-reboot", "-no-shutdown", "-m", "256M", "-vga", "std",
    "-device", "sb16", "-net", "nic,model=e1000", "-net", "user",
    "-debugcon", "file:" + dbg, "-serial", "file:" + log])
open(os.path.expandvars(r"%TEMP%\fx_qemu.pid"), "w").write(str(proc.pid))
print("qemu pid", proc.pid)

def cmd(s, c, wait=0.5):
    s.sendall((c + "\n").encode()); time.sleep(wait)
    try: return s.recv(65536).decode(errors="replace")
    except Exception: return ""

time.sleep(9)  # boot splash + wizard
s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
def keys(s, seq):
    for k in seq:
        cmd(s, "sendkey " + k, 0.12)
# wizard: user / user / user (confirm) / finish
keys(s, ["u","s","e","r","tab","u","s","e","r","tab","u","s","e","r","tab","ret"])
print("wizard done")
time.sleep(2.5)
# unlock #1
keys(s, ["u","s","e","r","ret"])
print("unlock1 sent")
time.sleep(2.5)
cmd(s, "screendump " + base + "_desk1.bmp", 1.2)
# relock via ctrl-alt-l
cmd(s, "sendkey ctrl-alt-l", 0.6)
print("relock sent")
time.sleep(2.5)
cmd(s, "screendump " + base + "_relock.bmp", 1.2)
# unlock #2 (the previously-failing path)
keys(s, ["u","s","e","r","ret"])
print("unlock2 sent")
time.sleep(2.5)
cmd(s, "screendump " + base + "_desk2.bmp", 1.2)
# stop and grab the definitive frame
cmd(s, "stop", 1.0)
time.sleep(0.5)
cmd(s, "screendump " + base + "_final.bmp", 1.2)
cmd(s, "pmemsave 0xfd000000 3145728 " + base + "_final_lfb.bin", 1.2)
s.close()
print("DONE")
