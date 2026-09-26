# unlock to desktop, compare LFB vs display, and query VGA info
import socket, time
PORT = 4459
def cmd(s, c, wait=0.5):
    s.sendall((c + "\n").encode())
    time.sleep(wait)
    try:
        return s.recv(65536).decode(errors="replace")
    except Exception:
        return ""
s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
print("unlock user:")
for k in "user":
    cmd(s, "sendkey %s" % k, 0.15)
cmd(s, "sendkey ret", 0.8)
time.sleep(3)
print("info vga:", cmd(s, "info vga", 1.0)[:500])
print("info display:", cmd(s, "info display", 1.0)[:300])
print("desk screendump:", cmd(s, "screendump D:/mycppos1/nefuOS/tools/v12_desk2.bmp", 1.0))
print("desk lfb:", cmd(s, "pmemsave 0xfd000000 3145728 D:/mycppos1/nefuOS/tools/v12_desk_lfb.bin", 1.0))
time.sleep(2)
print("desk screendump2:", cmd(s, "screendump D:/mycppos1/nefuOS/tools/v12_desk3.bmp", 1.0))
print("desk lfb2:", cmd(s, "pmemsave 0xfd000000 3145728 D:/mycppos1/nefuOS/tools/v12_desk_lfb2.bin", 1.0))
s.close()
print("DONE")
