# -*- coding: utf-8 -*-
# Use nefuOS internal --click injection (needs --shot to run the timed loop).
import os, subprocess, sys, time

EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8089/evt_test.html'
cx, cy = int(sys.argv[2]), int(sys.argv[3])
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'evt_shot.log')
bmp = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'evt_shot.bmp')

with open(log, 'w') as f:
    p = subprocess.Popen([EXE, '--app', '17', '--url', URL,
                          '--click', str(cx), str(cy),
                          '--shot', '17', bmp],
                         cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
# --click fires at ~1s, --click2 at 3s; shot loop exits at 6s
p.wait(timeout=15)
time.sleep(0.3)
txt = open(log, encoding='utf-8', errors='replace').read()
for line in txt.splitlines():
    if any(k in line for k in ('argv:', 'clicked', 'worker parsed', 'js dom re-render', 'via click', 'E: ')):
        print('LOG:', line.strip())
print('exit', p.returncode)
