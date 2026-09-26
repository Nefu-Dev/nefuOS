# -*- coding: utf-8 -*-
# load a real SPA page in nefuOS, capture log
import os, subprocess, sys, time

EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'https://chat.deepseek.com/'
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'spa.log')
bmp = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'spa.bmp')
with open(log, 'w') as f:
    p = subprocess.Popen([EXE, '--app', '17', '--url', URL, '--shot', '17', bmp],
                         cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
p.wait(timeout=25)
time.sleep(0.3)
txt = open(log, encoding='utf-8', errors='replace').read()
print('exit', p.returncode)
for line in txt.splitlines():
    if any(k in line for k in ('worker parsed', 're-render', 'E: ', 'load failed', 'fatal', 'truncat', 'Cannot')):
        print('LOG:', line.strip()[:180])
print('bmp exists:', os.path.exists(bmp))
