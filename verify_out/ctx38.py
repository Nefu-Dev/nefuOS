s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
# find all sV( calls
for m in re.finditer(r'\bsV\(', s):
    i = m.start()
    print('sV( at', i, ':', repr(s[max(0,i-100):i+100]))
