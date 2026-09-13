# nefuOS QMP UI driver: click / type / screenshot over QMP (test helper)
import socket, time, json, sys

def qmp(s, obj, wait=0.35):
    s.sendall((json.dumps(obj) + "\r\n").encode())
    time.sleep(wait)
    try:
        return s.recv(65536)
    except socket.timeout:
        return b""

def main():
    host, port = "127.0.0.1", 4444
    s = socket.create_connection((host, port), timeout=5)
    s.settimeout(1.0)
    s.recv(4096)
    qmp(s, {"execute": "qmp_capabilities"})

    # mode: shot | click x y | type "text" | key name
    args = sys.argv[1:]
    if not args:
        args = ["shot"]
    mode = args[0]
    if mode == "shot":
        qmp(s, {"execute": "screendump", "arguments": {"filename": "D:/mycppos1/nefuOS/build/bare/screen1.ppm"}}, wait=1.5)
        print("shot ok")
    elif mode == "click":
        x, y = int(args[1]), int(args[2])
        btn = int(args[3]) if len(args) > 3 else 1
        qmp(s, {"execute": "input-mouse-event", "arguments": {"type": "motion", "x": x, "y": y, "buttons": 0}})
        qmp(s, {"execute": "input-mouse-event", "arguments": {"type": "button", "button": btn, "down": True}})
        qmp(s, {"execute": "input-mouse-event", "arguments": {"type": "button", "button": btn, "down": False}})
        print("click ok", x, y, btn)
    elif mode == "type":
        text = args[1]
        for ch in text:
            if ch == " ":
                key = "spc"
            elif ch.isupper() or ch in "!@#$%^&*()_+{}|:\"<>?~":
                key = ch.lower()
                qmp(s, {"execute": "input-key-event", "arguments": {"type": "key", "key": {"type": "qcode", "name": "shift"}}})
                qmp(s, {"execute": "input-key-event", "arguments": {"type": "key", "key": {"type": "qcode", "name": key}}})
                qmp(s, {"execute": "input-key-event", "arguments": {"type": "key", "key": {"type": "qcode", "name": "shift"}, "down": False}})
                continue
            else:
                key = ch
            qmp(s, {"execute": "input-key-event", "arguments": {"type": "key", "key": {"type": "qcode", "name": key}}})
        print("type ok", text)
    elif mode == "key":
        name = args[1]
        qmp(s, {"execute": "input-key-event", "arguments": {"type": "key", "key": {"type": "qcode", "name": name}}})
        print("key ok", name)
    s.close()

main()
