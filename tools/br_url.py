import socket, sys, time, os

def send(mon, cmd, wait=0.4):
    mon.sendall(cmd.encode() + b'\n')
    time.sleep(wait)

def main(port, out):
    url = os.environ.get('BR_URL', 'nefu')
    s = socket.create_connection(('127.0.0.1', port), timeout=5)
    time.sleep(0.5)
    for ch in 'user':
        send(s, 'sendkey %s' % ch, 0.12)
    send(s, 'sendkey tab', 0.4)
    for ch in 'pass123':
        send(s, 'sendkey %s' % ch, 0.12)
    send(s, 'sendkey tab', 0.4)
    for ch in 'pass123':
        send(s, 'sendkey %s' % ch, 0.12)
    send(s, 'sendkey tab', 0.4)
    send(s, 'sendkey left', 0.3)
    send(s, 'sendkey right', 0.3)
    send(s, 'sendkey ret', 1.2)
    for ch in 'pass123':
        send(s, 'sendkey %s' % ch, 0.15)
    send(s, 'sendkey ret', 1.5)
    send(s, 'sendkey f6', 2.5)          # open browser
    # type into address bar (appends), then Enter
    for ch in url:
        send(s, 'sendkey %s' % ch, 0.12)
    send(s, 'sendkey ret', 2.0)
    send(s, 'pmemsave 0xFD000000 3145728 ' + out, 1.5)
    send(s, 'quit')
    s.close()
    print('browser input frame saved', out)

if __name__ == '__main__':
    main(int(sys.argv[1]), sys.argv[2])
