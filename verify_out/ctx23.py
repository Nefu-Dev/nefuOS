s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
nl = s.index('\n')
pos = nl + 1 + 271554
# find calls to PY( in a window after PY definition
window = s[pos:pos+8000]
for m in re.finditer(r'PY\(', window):
    i = m.start()
    print('PY( call at', i, ':', repr(window[max(0,i-60):i+60]))
