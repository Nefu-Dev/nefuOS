# STOP guest -> capture FINAL real state (is it desktop or lock?)
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
print("stop:", cmd(s, "stop", 1.0)[:60])
time.sleep(1)
print("final vga:", cmd(s, "screendump D:/mycppos1/nefuOS/tools/v12_final.bmp", 1.5)[:60])
print("final lfb:", cmd(s, "pmemsave 0xfd000000 3145728 D:/mycppos1/nefuOS/tools/v12_final_lfb.bin", 1.5)[:60])
s.close()
print("DONE")
