s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
nl = s.index('\n')
pos = nl + 1 + 3949271
for d in (-60, -30, 0, 30, 60):
    p = pos + d
    print('--- off', d, ':', repr(s[p-60:p+60]))
