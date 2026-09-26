# -*- coding: utf-8 -*-
# click to focus browser, then key tags to scroll
import os, subprocess, sys, time

EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8089/long_test.html'
tags = sys.argv[2] if len(sys.argv) > 2 else '<PgDn><PgDn>'
cx, cy = int(sys.argv[3]), int(sys.argv[4])
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'ck.log')
bmp = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'ck.bmp')
with open(log, 'w') as f:
    p = subprocess.Popen([EXE, '--app', '17', '--url', URL,
                          '--click', str(cx), str(cy),
                          '--key', tags, '--shot', '17', bmp],
                         cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
p.wait(timeout=20)
time.sleep(0.3)
txt = open(log, encoding='utf-8', errors='replace').read()
print('exit', p.returncode)
for line in txt.splitlines():
    if any(k in line for k in ('worker parsed', 'clicked', 'keys injected', 'browser: key', 'E: ')):
        print('LOG:', line.strip()[:140])
print('bmp', bmp)
