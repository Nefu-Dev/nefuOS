# -*- coding: utf-8 -*-
# click2 (3s) injection on long page - isolate key vs mouse
import os, subprocess, sys, time

EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8089/long_test.html'
cx, cy = int(sys.argv[2]), int(sys.argv[3])
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'click2.log')
bmp = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'click2.bmp')
with open(log, 'w') as f:
    p = subprocess.Popen([EXE, '--app', '17', '--url', URL,
                          '--click2', str(cx), str(cy), '--shot', '17', bmp],
                         cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
p.wait(timeout=20)
time.sleep(0.3)
txt = open(log, encoding='utf-8', errors='replace').read()
print('exit', p.returncode)
for line in txt.splitlines():
    if any(k in line for k in ('worker parsed', 'clicked', 'via click', 'hit run', 'E: ')):
        print('LOG:', line.strip()[:140])
