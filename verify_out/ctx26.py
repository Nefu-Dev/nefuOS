s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
nl = s.index('\n')
pos = nl + 1 + 271554
# all occurrences of PY after def
cnt = 0
for m in re.finditer(r'\bPY\b', s[pos:]):
    i = pos + m.start()
    print('PY at', i, ':', repr(s[i-70:i+50]))
    cnt += 1
    if cnt > 20: break
