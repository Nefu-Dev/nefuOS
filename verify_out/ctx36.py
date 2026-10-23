s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
cnt = 0
for m in re.finditer(r'querySelector', s):
    i = m.start()
    print('QS at', i, ':', repr(s[max(0,i-80):i+120]))
    cnt += 1
    if cnt >= 10: break
print('total:', len(re.findall(r'querySelector', s)))
