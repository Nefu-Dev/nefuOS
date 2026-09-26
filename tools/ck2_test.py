# -*- coding: utf-8 -*-
# click first (defocus address bar) then plain key string (with Enter)
import os, subprocess, sys, time

EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8089/long_test.html'
txt = sys.argv[2] if len(sys.argv) > 2 else 'abc'
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'ck2.log')
bmp = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'ck2.bmp')
with open(log, 'w') as f:
    p = subprocess.Popen([EXE, '--app', '17', '--url', URL,
                          '--click', '400', '300', '--key', txt, '--shot', '17', bmp],
                         cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
p.wait(timeout=20)
time.sleep(0.3)
txtlog = open(log, encoding='utf-8', errors='replace').read()
print('exit', p.returncode)
for line in txtlog.splitlines():
    if any(k in line for k in ('worker parsed', 'clicked', 'keys injected', 'via click', 'E: ')):
        print('LOG:', line.strip()[:140])
