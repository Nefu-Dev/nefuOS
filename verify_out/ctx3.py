import re
s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
# find where Oj is defined as a function
for m in re.finditer(r'(?:function\s+Oj|Oj\s*=\s*function|const\s+Oj\s*=\s*\()', s):
    i = m.start()
    print('Oj def at', i, ':', repr(s[i:i+300]))
    break
# find Vj=Oj(67) assignment
i = s.find('Vj=Oj(')
if i >= 0:
    print('Vj=Oj ctx:', repr(s[i-80:i+40]))
