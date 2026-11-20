# -*- coding: utf-8 -*-
# Bare-metal browser end-to-end check.
#
# Boots the real nefuOS bare-metal kernel in QEMU (with slirp networking so
# the guest has real Internet access), walks the first-boot setup wizard,
# opens the browser, types the bloxd.io URL and captures the framebuffer.
#
# This is the acceptance test for "the bare-metal browser can load bloxd.io":
# the framebuffer and the serial log together show whether the page really
# rendered, and the serial log carries the download/parse diagnostics.
#
# Usage:  python tools\bare_browser_test.py [--url URL] [--wait SECONDS]
#                                          [--shot NAME] [--keep]
#                                          [--accel ACCEL]  (tcg,thread=multi | whpx)
import socket, subprocess, time, os, sys, shutil

ROOT = r"D:\mycppos1\nefuOS"
Q = r"D:\qemu\qemu-system-x86_64.exe"
# build_iso_bareonly.ps1 writes the image into <root>\dist\nefuOS.iso. Booting
# a stale copy from %TEMP% silently runs the *previous* kernel and makes every
# newly added diagnostic look like it never fired, so point at the build output
# directly and fall back to the old temp location only if it is missing.
_BUILD_ISO = os.path.join(ROOT, "dist", "nefuOS.iso")
_TEMP_ISO = os.path.join(os.environ.get("TEMP", r"C:\Windows\Temp"), "nefu_dist", "nefuOS.iso")
ISO = _BUILD_ISO if os.path.exists(_BUILD_ISO) else _TEMP_ISO
BASE = os.path.join(ROOT, "build", "bare")
PORT = 4461
MEM = "3072"          # the browser arena lives at 0x40000000 and is 2 GB
CLICK_LABEL = ""       # label to press on the page ("Agree", "Play", ...)

# The page actions to perform, in order. Each is one of:
#   <label>       click the element showing that text
#   key:<Name>    press a key           ("key:Escape")
#   text:<Label>  click visible text    ("text:Play")
#   class:<Frag>  click by class name   ("class:PromptGenericPopupBodyButton")
#
# bloxd.io stacks several dialogs on top of each other: a floating one that
# the Escape key dismisses, then the privacy consent dialog, and only then
# the game itself. The sequence is therefore driven from the terminal.
ACTIONS = []


def mon(cmd, wait=0.35):
    try:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=4)
        s.settimeout(4)
        s.sendall((cmd + "\n").encode())
        time.sleep(wait)
        s.close()
    except Exception as e:
        print("mon err:", cmd, e)


def sendkey(k, wait=0.12):
    mon("sendkey " + k, wait)


def sendtext(t, wait=0.10):
    for ch in t:
        if ch == " ":
            sendkey("spc")
        elif ch == ".":
            sendkey("dot")
        elif ch == "/":
            sendkey("slash")
        elif ch == "-":
            sendkey("minus")
        elif ch == "_":
            sendkey("shift-minus")
        elif ch == "=":
            sendkey("equal")
        elif ch == ",":
            sendkey("comma")
        elif ch == ":":
            sendkey("shift-semicolon")
        elif ch.isupper():
            sendkey("shift-" + ch.lower())
        elif ch.isdigit():
            sendkey(ch)
        else:
            sendkey(ch)
        time.sleep(wait)
    sendkey("ret", 0.6)


def grab(name):
    """Save the linear framebuffer and convert it to a BMP."""
    out = os.path.join(BASE, name + ".bin")
    mon("pmemsave 0xFD000000 %d %s" % (1024 * 768 * 4, out), 2.0)
    time.sleep(0.6)
    bmp = os.path.join(BASE, name + ".bmp")
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "lfb2bmp.py"),
                        out, bmp], capture_output=True)
    if r.returncode != 0:
        print("lfb2bmp failed:", r.stderr.decode(errors="replace")[:200])
        return None
    return bmp


def serial_tail(path, n=40):
    if not os.path.exists(path):
        return ""
    d = open(path, "rb").read().decode("utf-8", "replace")
    return "\n".join(d.strip().splitlines()[-n:])


def wait_ready(uart, timeout=180):
    """Wait until the kernel has finished its boot selftests.

    A fixed sleep is not reliable: the ELF/DEB/X11/llm selftests run before
    the setup wizard is drawn and their duration varies a lot with host load.
    Typing the wizard keys too early loses every keystroke, leaving the run
    stuck on the First Boot Setup screen with no browser and no HTTP logs.
    'nefuOS ready on bare' is the last init message before the wizard is up.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(uart):
            try:
                d = open(uart, "rb").read().decode("utf-8", "replace")
                if "nefuOS ready on bare" in d:
                    return True
            except OSError:
                pass
        time.sleep(1)
    print("WARN: boot readiness not seen within %ds, continuing anyway" % timeout)
    return False


def wait_uart(uart, needle, timeout=20):
    """Wait until `needle` shows up in the serial log."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(uart):
            try:
                d = open(uart, "rb").read().decode("utf-8", "replace")
                if needle in d:
                    return True
            except OSError:
                pass
        time.sleep(0.5)
    return False


def open_browser_via_terminal(uart, url, actions):
    """Type the terminal `open` command, verifying it actually landed.

    Under WHPX the guest event loop processes keystrokes at unpredictable
    times: a fixed-sleep run can type the whole command while the wizard is
    still up or between frames, and every keystroke is lost. 'app_launch'
    (F2 opens the terminal) and "term: run" (the command executed) are the
    serial markers, so poll for them and retry a few times when they do not
    appear.
    """
    cmd = "open " + url
    for act in actions:
        cmd += " " + act
    for attempt in range(4):
        print("   attempt %d: %s" % (attempt + 1, repr(cmd)))
        sendkey("f2", 3.0)
        if not wait_uart(uart, "app_launch", 12):
            print("   no app_launch marker, retrying ...")
            sendkey("esc", 1.0)
            continue
        sendtext(cmd)
        if wait_uart(uart, "term: run", 15):
            print("   command accepted")
            return True
        print("   command not executed, retrying ...")
        sendkey("esc", 1.0)
    print("   WARNING: terminal command never confirmed")
    return False


def main():
    url = "https://bloxd.io/"
    wait = 70
    shot = "bare_bloxd"
    keep = False
    accel = "tcg,thread=multi"   # --accel whpx uses WHPX hardware virtualization
    click_label = ""          # e.g. "Agree" or "Play"
    actions = []              # page actions, in order
    args = sys.argv[1:]
    for i, a in enumerate(args):
        if a == "--url" and i + 1 < len(args):
            url = args[i + 1]
        elif a == "--wait" and i + 1 < len(args):
            wait = int(args[i + 1])
        elif a == "--accel" and i + 1 < len(args):
            accel = args[i + 1]
        elif a == "--shot" and i + 1 < len(args):
            shot = args[i + 1]
        elif a == "--keep":
            keep = True
        elif a == "--click" and i + 1 < len(args):
            click_label = args[i + 1]
        elif a == "--actions" and i + 1 < len(args):
            # comma separated: "key:Escape,class:Foo,text:Play"
            for act in args[i + 1].split(","):
                act = act.strip()
                if act:
                    actions.append(act)

    global CLICK_LABEL, ACTIONS
    CLICK_LABEL = click_label
    ACTIONS = actions

    if not os.path.exists(Q):
        print("NO QEMU:", Q)
        return 1
    if not os.path.exists(ISO):
        print("NO ISO:", ISO)
        return 1
    os.makedirs(BASE, exist_ok=True)

    subprocess.run(["taskkill", "/F", "/IM", "qemu-system-x86_64.exe"], capture_output=True)
    time.sleep(1)

    uart = os.path.join(BASE, "bare_browser_uart.txt")
    # The bare-metal libc routes printf/fprintf(stderr) to the QEMU debug
    # console (port 0xE9), not to the serial port. The browser and the JS
    # engine log all of their diagnostics there, so it has to be captured as
    # well or the run is completely opaque.
    dbg = os.path.join(BASE, "bare_browser_dbg.txt")
    for f in (uart, dbg):
        if os.path.exists(f):
            os.remove(f)

    p = subprocess.Popen([
        Q, "-cdrom", ISO, "-boot", "d", "-m", MEM,
        "-accel", accel,
        "-netdev", "user,id=n0", "-device", "e1000,netdev=n0",
        "-monitor", "tcp:127.0.0.1:%d,server,nowait" % PORT,
        "-serial", "file:" + uart, "-debugcon", "file:" + dbg,
        "-display", "none", "-no-reboot",
    ], creationflags=0x08000000)   # CREATE_NO_WINDOW

    print("QEMU pid", p.pid, "waiting for boot ...")
    wait_ready(uart, 180)
    # The wizard is drawn on the main loop right after the init banner; give
    # the compositor a couple of frames before injecting the first keystroke.
    time.sleep(4)

    # ---- first-boot wizard: user / password / confirm / engine / finish ----
    print("setup wizard ...")
    sendtext("user")
    time.sleep(2.0)
    sendtext("pass123")
    time.sleep(2.0)
    sendtext("pass123")
    time.sleep(2.0)
    sendkey("ret", 3.0)       # engine choice -> next field
    sendkey("ret", 4.0)       # finish -> the desktop is then covered by the
                              # lock screen, which wants the password again
    time.sleep(3)

    # ---- unlock ----
    print("unlocking ...")
    sendtext("pass123")
    time.sleep(4)
    grab(shot + "_desktop")

    # ---- open the browser through the terminal ----
    # The terminal's "open" command starts the browser at a URL and can press
    # a labelled button once the page is live. Driving it that way is far
    # more reliable than typing into the address bar, because the terminal
    # does not depend on the window focus and the label is matched by text
    # instead of by a DOM index that changes with every render.
    print("opening browser via terminal ...")
    grab(shot + "_desktop")
    open_browser_via_terminal(uart, url, ACTIONS)
    time.sleep(3)
    grab(shot + "_browser")
    # The "open" command hands the screen to the browser and steps the shell
    # aside, so there is nothing left to close here. The page keeps loading
    # on the main loop (3 MB bundle + React mount), so give it real time.
    time.sleep(60)

    # The page load runs on the main loop, so give it real time: the main
    # bundle alone is 3 MB and the React mount needs the engine budget.
    # While waiting, watch the serial log so the action sequence can be
    # followed live instead of only at the very end.
    print("waiting %ds for the page ..." % wait)
    deadline = time.time() + wait
    step = 12
    # Per-file offsets: the debug console starts with a long binary banner, so
    # a single shared offset silently hid every new line from the shorter file.
    seen = {uart: 0, dbg: 0}
    watch = ("[ACT", "[KEY", "[CLICK", "ACTDONE", "page action", "term:",
             "hb: OK", "hb: wait", "hb: req", "hb: body plan",
             "hb: bodyfnv", "hb: .*FAIL", "KALLOC", "KFREE",
             "browser: module", "browser: moddiag", "  md ",
             "browser: src[", "browser: fetching")
    while time.time() < deadline:
        time.sleep(step)
        grab(shot + "_page")
        # print the new lines of the two log channels as they appear
        for path, tag in ((uart, "uart"), (dbg, "dbg")):
            if not os.path.exists(path):
                continue
            data = open(path, "rb").read().decode("utf-8", "replace")
            if len(data) > seen.get(path, 0):
                chunk = data[seen.get(path, 0):]
                seen[path] = len(data)
                for ln in chunk.splitlines():
                    if any(k in ln for k in watch):
                        print("  [%s] %s" % (tag, ln[:150]))
        print("  ... still loading (%d s left)" % max(0, int(deadline - time.time())))

    bmp = grab(shot + "_final")
    print("final framebuffer:", bmp)
    # a cropped copy of the browser area, so the page can be inspected
    # without the rest of the desktop in the way
    try:
        from PIL import Image
        im = Image.open(bmp)
        im.crop((0, 0, 1024, 768)).save(os.path.join(BASE, shot + "_full.png"))
        im.crop((0, 0, 800, 600)).resize((1200, 900), Image.LANCZOS).save(
            os.path.join(BASE, shot + "_zoom.png"))
        print("cropped:", shot + "_zoom.png")
    except Exception as e:
        print("crop failed:", e)

    print("=== serial tail ===")
    print(serial_tail(uart, 30))
    print("=== debug console (browser / JS engine) ===")
    print(serial_tail(dbg, 80))

    if not keep:
        subprocess.run(["taskkill", "/F", "/IM", "qemu-system-x86_64.exe"], capture_output=True)
    else:
        print("QEMU left running (pid %d)" % p.pid)
    return 0


if __name__ == "__main__":
    sys.exit(main())
