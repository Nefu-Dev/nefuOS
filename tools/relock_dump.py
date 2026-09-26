# Relock + dump LFB memory and VGA screendump for comparison
import socket, time, os, sys
PORT = 4459
def cmd(s, c, wait=0.5):
    s.sendall((c + "\n").encode())
    time.sleep(wait)
    try:
        return s.recv(65536).decode(errors="replace")
    except Exception:
        return ""
s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
print("relock:", cmd(s, "sendkey ctrl-alt-l", 1.0))
time.sleep(2)
print("vga dump:", cmd(s, "screendump D:/mycppos1/nefuOS/tools/v12_relock.bmp", 1.0))
print("lfb dump:", cmd(s, "pmemsave 0xfd000000 3145728 D:/mycppos1/nefuOS/tools/v12_lfb.bin", 1.0))
time.sleep(3)
print("vga dump2:", cmd(s, "screendump D:/mycppos1/nefuOS/tools/v12_relock2.bmp", 1.0))
print("lfb dump2:", cmd(s, "pmemsave 0xfd000000 3145728 D:/mycppos1/nefuOS/tools/v12_lfb2.bin", 1.0))
s.close()
print("DONE")
