s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
# find definitions of mb
for pat in [r'function mb', r'mb\s*=\s*function', r'mb\s*=\s*\(', r'\bmb:\s*\(\)']:
    for m in re.finditer(pat, s):
        print(pat, 'at', m.start(), ':', repr(s[m.start()-60:m.start()+80]))
        if m.start() > 4000000: break
