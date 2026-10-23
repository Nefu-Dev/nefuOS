s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
nl = s.index('\n')
pos = nl + 1 + 271554
seg = s[pos-2500:pos]
# find module head
import re
for m in re.finditer(r'\(jb,bj,Oj\)=>', seg):
    print('module head at rel', m.start()-2500, ':', repr(seg[m.start():m.start()+400]))
