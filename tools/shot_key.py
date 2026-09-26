# -*- coding: utf-8 -*-
# internal --key with <PgDn> tags for scroll verification
import os, subprocess, sys, time

EXE = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'nefuOS.exe')
URL = sys.argv[1] if len(sys.argv) > 1 else 'http://127.0.0.1:8089/long_test.html'
tags = sys.argv[2] if len(sys.argv) > 2 else '<PgDn><PgDn><PgDn>'
out_bmp = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'scroll_int.bmp')
log = os.path.join(os.environ['TEMP'], 'nefu_fix_test', 'scroll_int.log')

with open(log, 'w') as f:
    p = subprocess.Popen([EXE, '--app', '17', '--url', URL,
                          '--key', tags, '--shot', '17', out_bmp],
                         cwd=os.path.join(os.environ['TEMP'], 'nefu_fix_test'),
                         stdout=f, stderr=subprocess.STDOUT)
p.wait(timeout=20)
time.sleep(0.3)
txt = open(log, encoding='utf-8', errors='replace').read()
print('exit', p.returncode)
for line in txt.splitlines():
    if any(k in line for k in ('worker parsed', 'keys injected', 'key ', 'E: ', 'fatal')):
        print('LOG:', line.strip()[:140])
print('bmp', out_bmp)
