"""Drive an already-running nefuOS QEMU instance through the GL probe page.

The regular harness (bare_browser_test.py) starts QEMU itself via Popen with
CREATE_NO_WINDOW. On this host that child dies immediately (no console, so no
monitor port ever opens), which makes every run look like a boot hang. Starting
QEMU from a normal shell works fine, so this driver keeps that split: QEMU is
launched separately and this script only injects keystrokes through the monitor
socket and saves screenshots.

usage: python tools/_drive_gl.py <url> <shot-tag>
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bare_browser_test as H


def main(argv):
    url = argv[1]
    shot = argv[2]
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
    H.grab(shot + "_desktop")

    print("opening browser via terminal ...")
    ok = H.open_browser_via_terminal(uart, url, [])
    print("terminal command confirmed:", ok)
    time.sleep(3)
    H.grab(shot + "_browser")

    print("waiting for the page ...")
    for i in range(12):
        time.sleep(10)
        H.grab(shot + "_page")
        print("  ... %ds" % (10 * (i + 1)))
    H.grab(shot + "_final")
    print("drove the run to completion")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
