# -*- coding: utf-8 -*-
import io, re, sys
sys.stdout.reconfigure(encoding='utf-8')
t = io.open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js.bak_premark', encoding='utf-8').read()
pat = re.compile(r'\.exec\(')
count = 0
for m in pat.finditer(t):
    i = m.start()
    ctx = t[max(0,i-120):i+60]
    if '"' in ctx or '&' in ctx or '<' in ctx:
        print(i, ':', ctx[:180].replace(chr(10),' '))
        count += 1
        if count >= 10:
            break
print('total shown', count)
