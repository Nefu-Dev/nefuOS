import re
s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
for pat in ['"67":[', '67:[', "67:["]:
    i = s.find(pat)
    if i >= 0:
        print('found', pat, 'at', i)
        print(repr(s[i:i+800]))
        break
else:
    print('module 67 not found')
