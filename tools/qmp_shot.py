import socket, time, sys

def qmp_cmd(s, obj):
    s.sendall((json_dumps(obj) + "\r\n").encode())
    time.sleep(0.5)
    return s.recv(65536)

import json
json_dumps = json.dumps

s = socket.create_connection(("127.0.0.1", 4444), timeout=5)
print(s.recv(4096))
s.sendall(b'{"execute":"qmp_capabilities"}\r\n')
time.sleep(0.5)
print(s.recv(4096))
s.sendall(b'{"execute":"screendump","arguments":{"filename":"D:/mycppos1/nefuOS/build/bare/screen1.ppm"}}\r\n')
time.sleep(2.0)
print(s.recv(65536))
s.close()
