s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
nl = s.index('\n')
pos = nl + 1 + 271554
# find tY= or tY: definitions within 30KB before pos
import re
seg = s[max(0,pos-30000):pos]
for m in re.finditer(r'\btY\s*=', seg):
    i = m.start()
    print('tY= at rel', i-30000, ':', repr(seg[max(0,i-80):i+100]))
for m in re.finditer(r'tY\s*:\s*', seg):
    i = m.start()
    print('tY: at rel', i-30000, ':', repr(seg[max(0,i-60):i+60]))
