import socket, sys, time, os

def send(mon, cmd, wait=0.4):
    mon.sendall(cmd.encode() + b'\n')
    time.sleep(wait)

def main(port, out):
    mode = os.environ.get('WIN_MODE', 'open')
    s = socket.create_connection(('127.0.0.1', port), timeout=5)
    time.sleep(0.5)
    # first boot setup: user / pass123
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
    # unlock lock screen
    for ch in 'pass123':
        send(s, 'sendkey %s' % ch, 0.15)
    send(s, 'sendkey ret', 1.2)
    if mode == 'open':
        send(s, 'sendkey f3', 1.2)   # File Manager
        send(s, 'pmemsave 0xFD000000 3145728 ' + out, 1.5)
    else:
        send(s, 'sendkey f3', 1.2)   # open File Manager first
        cx = int(os.environ.get('CLOSE_X', '0'))
        cy = int(os.environ.get('CLOSE_Y', '0'))
        send(s, 'mouse_move -2000 -2000', 0.6)   # snap to top-left
        send(s, 'mouse_move %d %d' % (cx, cy), 0.5)
        send(s, 'mouse_button 1', 0.3)
        send(s, 'mouse_button 0', 1.0)
        send(s, 'pmemsave 0xFD000000 3145728 ' + out, 1.5)
    send(s, 'quit')
    s.close()
    print(mode, 'frame saved', out)

if __name__ == '__main__':
    main(int(sys.argv[1]), sys.argv[2])
