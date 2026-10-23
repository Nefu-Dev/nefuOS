s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
i = s.find('Xj=function()')
if i >= 0:
    print(repr(s[i:i+1200]))
else:
    print('not found')
