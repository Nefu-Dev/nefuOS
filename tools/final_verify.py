# -*- coding: utf-8 -*-
import socket, subprocess, time, os, sys, shutil

Q = r"C:\msys64\mingw64\bin\qemu-system-x86_64.exe"
ISO = r"D:\mycppos1\nefuOS\dist\nefuOS-v13.iso"
BASE = r"D:\mycppos1\nefuOS\build\bare"
PORT = 4444

def mon(cmd, wait=0.8):
    try:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=3)
        s.settimeout(3)
        s.sendall((cmd + "\n").encode())
        time.sleep(wait)
        s.close()
    except Exception as e:
        print("mon err", cmd, e)

def sendtext(t):
    for ch in t:
        if ch.isupper(): mon("sendkey shift-" + ch.lower())
        else: mon("sendkey " + ch)
    mon("sendkey ret")

# launch
subprocess.run(["taskkill", "/F", "/IM", "qemu-system-x86_64.exe"], capture_output=True)
time.sleep(1)
uart = os.path.join(BASE, "uart_final.txt")
dbg = os.path.join(BASE, "dbg_final.txt")
for f in (uart, dbg):
    if os.path.exists(f): os.remove(f)
p = subprocess.Popen([Q, "-cdrom", ISO, "-m", "128", "-display", "none",
    "-monitor", "tcp:127.0.0.1:%d,server,nowait" % PORT,
    "-serial", "file:" + uart, "-debugcon", "file:" + dbg, "-no-reboot"])
time.sleep(14)

def grab(name, addr, size):
    out = os.path.join(BASE, name + ".bin")
    mon("pmemsave 0x%X %d %s" % (addr, size, out), 2)
    time.sleep(0.5)
    bmp = os.path.join(BASE, name + ".bmp")
    subprocess.run([sys.executable, r"D:\mycppos1\nefuOS\tools\lfb2bmp.py", out, bmp], capture_output=True)
    return bmp

bmp1 = grab("final_lock", 0xFD000000, 1024*768*4)
print("lock screen captured")

# unlock: user / pass123
sendtext("user")
time.sleep(2)
sendtext("pass123")
time.sleep(4)
bmp2 = grab("final_desktop", 0xFD000000, 1024*768*4)
print("desktop captured")

# F6 -> browser
mon("sendkey f6")
time.sleep(6)
bmp3 = grab("final_browser", 0xFD000000, 1024*768*4)
print("browser captured")

# F7 -> editor
mon("sendkey f7")
time.sleep(5)
bmp4 = grab("final_editor", 0xFD000000, 1024*768*4)
print("editor captured")

subprocess.run(["taskkill", "/F", "/IM", "qemu-system-x86_64.exe"], capture_output=True)
print("uart tail:")
if os.path.exists(uart):
    d = open(uart, "rb").read().decode("utf-8", "replace")
    print("\n".join(d.strip().splitlines()[-6:]))
print("done", bmp1, bmp2, bmp3, bmp4)
