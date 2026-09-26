# DECISIVE: stop the guest, then compare VGA screendump vs LFB memory
import socket, time
PORT = 4459
def cmd(s, c, wait=0.6):
    s.sendall((c + "\n").encode())
    time.sleep(wait)
    try:
        return s.recv(65536).decode(errors="replace")
    except Exception:
        return ""
s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
print("stop:", cmd(s, "stop", 1.0)[:200])
time.sleep(1)
print("vga stopped:", cmd(s, "screendump D:/mycppos1/nefuOS/tools/v12_stop.bmp", 1.5)[:80])
print("lfb stopped:", cmd(s, "pmemsave 0xfd000000 3145728 D:/mycppos1/nefuOS/tools/v12_stop_lfb.bin", 1.5)[:80])
time.sleep(1)
print("vga stopped2:", cmd(s, "screendump D:/mycppos1/nefuOS/tools/v12_stop2.bmp", 1.5)[:80])
print("cont:", cmd(s, "cont", 0.5)[:80])
s.close()
print("DONE")
