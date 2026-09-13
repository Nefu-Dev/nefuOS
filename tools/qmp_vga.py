import socket, time, json

s = socket.create_connection(("127.0.0.1", 4444), timeout=5)
print(s.recv(4096))
s.sendall(b'{"execute":"qmp_capabilities"}\r\n')
time.sleep(0.5)
print(s.recv(4096))
s.sendall(b'{"execute":"human-monitor-command","arguments":{"command-line":"info vga"}}\r\n')
time.sleep(0.8)
print(s.recv(65536))
s.close()
