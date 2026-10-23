s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
nl = s.index('\n')
pos = nl + 1 + 271554
# all PY( calls in whole file after pos
calls = [m.start() for m in re.finditer(r'PY\(', s)]
for i in calls:
    if i > pos:
        print('PY( at', i, ':', repr(s[i-80:i+60]))
        break
