"""Drive an already-running nefuOS QEMU instance to run the `nettest` deep
network self-test and print the Colyseus handshake verdict.

Companion to _drive_gl.py. QEMU must already be running (started from a normal
shell, proxy env cleared) with its serial log at build/bare/bare_browser_uart.txt
and its monitor on 127.0.0.1:4461.

What it does:
  boot-ready -> wizard -> unlock -> F2 terminal -> type `nettest` ->
  wait for "net-selftest: deep done" -> print the matchmake / JOIN_ROOM /
  "ws msg ... op=... code=... msg=..." lines from the serial log.

The last lines are the whole point: op=ROOM_JOIN(10)/ROOM_STATE(14) means the
join succeeded; op=ERROR(11) code=525 msg=onAuth failed means the game server
rejected the (anonymous) identity, which pins the remaining work to auth.

usage: python tools/_drive_nettest.py [shot-tag]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bare_browser_test as H

NEEDLES = [
    "net-selftest: bloxd bundle OK",
    "net-selftest: bloxd bundle FAIL",
    # Firebase anonymous sign-in is the real bloxd identity: its status decides
    # whether the 401 that follows is an auth problem or a transport problem.
    "net-selftest: firebase anon",
    "net-selftest: firebase body",
    "net-selftest: matchmake",
    "net-selftest: matchmake body",
    "JOIN_ROOM",
    "websocket open",
    "websocket no message",
    "ws msg",
    "hb: WARN",
    "net-selftest: deep done",
]


def dump_uart(uart, tag):
    try:
        with open(uart, "r", errors="replace") as f:
            lines = [ln.rstrip("\n") for ln in f]
    except OSError:
        print("WARN: serial log not readable:", uart)
        return
    print("\n==== %s: nettest Colyseus verdict ====" % tag)
    hits = [ln for ln in lines if any(n in ln for n in NEEDLES)]
    for ln in hits[-50:]:
        print(ln)
    if not hits:
        print("(no net-selftest lines found - did the command run?)")
    print("==== end %s ====" % tag)


def run_in_terminal(uart, command):
    """Open the F2 terminal and type `command`, verifying it executed."""
    for attempt in range(4):
        print("   attempt %d: %r" % (attempt + 1, command))
        H.sendkey("f2", 3.0)
        if not H.wait_uart(uart, "app_launch", 12):
            print("   no app_launch marker, retrying ...")
            H.sendkey("esc", 1.0)
            continue
        H.sendtext(command)
        if H.wait_uart(uart, "term: run", 15):
            print("   command accepted")
            return True
        print("   command not executed, retrying ...")
        H.sendkey("esc", 1.0)
    print("   WARNING: terminal command never confirmed")
    return False


def main(argv):
    tag = argv[1] if len(argv) > 1 else "nettest"
    uart = os.path.join(H.BASE, "bare_browser_uart.txt")

    print("waiting for boot ...")
    if not H.wait_ready(uart, 240):
        print("WARN: boot readiness not seen")
    time.sleep(4)

    print("setup wizard ...")
    H.sendtext("user");     time.sleep(2.0)
    H.sendtext("pass123");  time.sleep(2.0)
    H.sendtext("pass123");  time.sleep(2.0)
    H.sendkey("ret", 3.0)
    H.sendkey("ret", 4.0)
    time.sleep(3)

    print("unlocking ...")
    H.sendtext("pass123");  time.sleep(4)
    try:
        H.grab(tag + "_desktop")
    except Exception:
        pass

    print("running nettest in terminal ...")
    ok = run_in_terminal(uart, "nettest")
    print("nettest command accepted:", ok)

    print("waiting for the deep checks (bundle ~15-40 s, then matchmake + WS) ...")
    if H.wait_uart(uart, "net-selftest: deep done", 200):
        print("deep run finished")
    else:
        print("WARN: 'deep done' not seen within 200 s (network may be down)")

    dump_uart(uart, tag)
    try:
        H.grab(tag + "_final")
    except Exception:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
