# Test if guest is alive: continue + send key + check serial growth
import socket, time, os
PORT = 4459
def cmd(s, c, wait=0.5):
    s.sendall((c + "\n").encode())
    time.sleep(wait)
    try:
        return s.recv(65536).decode(errors="replace")
    except Exception:
        return ""
s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
sz0 = os.path.getsize("D:/mycppos1/nefuOS/tools/v12_serial.log")
print("serial size before:", sz0)
print("cont:", cmd(s, "cont", 0.5)[:40])
time.sleep(2)
print("send a:", cmd(s, "sendkey a", 0.5)[:40])
time.sleep(3)
sz1 = os.path.getsize("D:/mycppos1/nefuOS/tools/v12_serial.log")
print("serial size after:", sz1, "delta:", sz1 - sz0)
s.close()
print("DONE")
