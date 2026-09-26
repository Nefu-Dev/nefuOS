# -*- coding: utf-8 -*-
# shot a URL via the internal --shot loop (6s), with optional internal click
import os, subprocess, sys, time

EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8089/long_test.html'
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'shot_%d.log' % os.getpid())
bmp = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'shot_%d.bmp' % os.getpid())
args = [EXE, '--app', '17', '--url', URL, '--shot', '17', bmp]
with open(log, 'w') as f:
    p = subprocess.Popen(args, cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
p.wait(timeout=20)
time.sleep(0.3)
txt = open(log, encoding='utf-8', errors='replace').read()
print('exit', p.returncode)
for line in txt.splitlines():
    if any(k in line for k in ('worker parsed', 'E: ', 'fatal', 'via click', 'truncat')):
        print('LOG:', line.strip()[:160])
print('bmp', bmp)
