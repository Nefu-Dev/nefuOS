s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
nl = s.index('\n')
pos = nl + 1 + 271554
# search backward for tY= and QY= definitions
for name in ['tY', 'QY']:
    for m in re.finditer(r'(?:var|let|const)\s+' + name + r'\s*=', s[pos-20000:pos]):
        i = m.start()
        print(name, 'def at rel', i-20000, ':', repr(s[pos-20000+i-40:pos-20000+i+120]))
