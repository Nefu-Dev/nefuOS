s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
nl = s.index('\n')
pos = nl + 1 + 3949271
seg = s[pos-6000:pos+3000]
import re
for m in re.finditer(r'\bmb\b', seg):
    i = m.start()
    print('mb at rel', i-6000, ':', repr(seg[max(0,i-70):i+70]))
