s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
nl = s.index('\n')
pos = nl + 1 + 271554
for d in (-70, -35, 0, 35, 70):
    p = pos + d
    print('--- off', d, ':', repr(s[p-70:p+70]))
