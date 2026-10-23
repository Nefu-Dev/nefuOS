s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
nl = s.index('\n')
pos = nl + 1 + 2706124
print('line1 len:', nl)
for d in (-80, -40, 0, 40, 80):
    p = pos + d
    print('--- off', d, ':', repr(s[p-60:p+60]))
