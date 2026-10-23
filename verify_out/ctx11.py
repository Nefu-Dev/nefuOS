s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
# find all call sites of Xj( after the definition
import re
for m in re.finditer(r'Xj\(\)', s):
    i = m.start()
    if i < 26000: continue
    print('Xj() call at', i, ':', repr(s[i-120:i+80]))
    if i > 300000: break
