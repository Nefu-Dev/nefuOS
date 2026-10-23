s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
for m in re.finditer(r'Kj\(227', s):
    i = m.start()
    print('227 at', i, ':', repr(s[max(0,i-260):i+80]))
