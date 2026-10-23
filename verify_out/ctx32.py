s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
nl = s.index('\n')
h = 225874
# search PY( and PY within module [h, 271554+30000]
seg = s[h:271554+30000]
for m in re.finditer(r'\bPY\(', seg):
    i = h + m.start()
    print('PY( at', i, ':', repr(seg[max(0,m.start()-100):m.start()+80]))
