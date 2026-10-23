s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
import re
nl = s.index('\n')
h = 225874
seg = s[h:h+80000]
for name in ['vV(', 'UV(', 'qV(', 'listenToAllSupportedEvents', 'jbV(', 'listenTo']:
    cnt = 0
    for m in re.finditer(re.escape(name), seg):
        i = h + m.start()
        print(name, 'at', i, ':', repr(seg[max(0,m.start()-60):m.start()+100]))
        cnt += 1
        if cnt >= 8: break
    print('---')
