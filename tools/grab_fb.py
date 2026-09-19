import socket, sys, struct, time

def grab(port, out):
    s = socket.create_connection(('127.0.0.1', port), timeout=5)
    time.sleep(0.2)
    s.sendall(b'pmemsave 0xFD000000 3145728 ' + out.encode() + b'\n')
    time.sleep(1.0)
    s.sendall(b'quit\n')
    s.close()

if __name__ == '__main__':
    grab(int(sys.argv[1]), sys.argv[2])
    print('grabbed', sys.argv[2])
