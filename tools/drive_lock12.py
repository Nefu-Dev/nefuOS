# Drive QEMU monitor: boot new ISO -> wizard -> lock -> screencap -> unlock -> desktop
import socket, sys, time, os

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 4459
SHOT = sys.argv[2] if len(sys.argv) > 2 else "lock12"

def cmd(s, c):
    s.sendall((c + "\n").encode())
    time.sleep(0.35)
    try:
        return s.recv(65536).decode(errors="replace")
    except Exception:
        return ""

def wait_ready(s, tries=60):
    for _ in range(tries):
        try:
            s.sendall(b"info status\n")
            time.sleep(0.3)
            s.settimeout(0.5)
            r = s.recv(65536).decode(errors="replace")
            if "running" in r or "status" in r:
                return True
        except Exception:
            pass
        time.sleep(1)
    return False

def main():
    s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
    if not wait_ready(s):
        print("MONITOR NOT READY")
        return
    time.sleep(2)  # BIOS/boot
    # Wait for wizard: the boot splash lasts ~1.6s + boot. Send keys slowly.
    time.sleep(6)
    print("sending wizard keys")
    cmd(s, "sendkey u")
    cmd(s, "sendkey s")
    cmd(s, "sendkey e")
    cmd(s, "sendkey r")
    cmd(s, "sendkey tab")
    cmd(s, "sendkey u")
    cmd(s, "sendkey s")
    cmd(s, "sendkey e")
    cmd(s, "sendkey r")
    cmd(s, "sendkey tab")
    cmd(s, "sendkey u")
    cmd(s, "sendkey s")
    cmd(s, "sendkey e")
    cmd(s, "sendkey r")
    cmd(s, "sendkey tab")
    time.sleep(0.5)
    cmd(s, "sendkey ret")   # finish wizard -> lock screen
    time.sleep(3)
    print("lock screencap")
    cmd(s, "screendump %s" % os.path.abspath("tools\\%s_lock.bmp" % SHOT))
    time.sleep(1)
    # unlock: type user + Enter
    cmd(s, "sendkey u")
    cmd(s, "sendkey s")
    cmd(s, "sendkey e")
    cmd(s, "sendkey r")
    cmd(s, "sendkey ret")
    time.sleep(3)
    print("desktop screencap")
    cmd(s, "screendump %s" % os.path.abspath("tools\\%s_desk.bmp" % SHOT))
    time.sleep(1)
    s.close()
    print("DONE")

if __name__ == "__main__":
    main()
