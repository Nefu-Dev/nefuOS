s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
nl = s.index('\n')
# find createRoot usages
cnt = 0
for m in re.finditer(r'createRoot', s):
    i = m.start()
    seg = s[max(0,i-120):i+160]
    print('createRoot at', i, ':', repr(seg[:240]))
    cnt += 1
    if cnt > 12: break
print('total createRoot:', len(re.findall(r'createRoot', s)))
