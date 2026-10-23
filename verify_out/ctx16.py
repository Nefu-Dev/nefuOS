s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
# QuickJS: <script>:2:26014 -> line 2, byte col 26014 (1-based). line 1 = up to first \n
nl = s.index('\n')
print('line1 len:', nl)
pos = nl + 1 + 26014  # approx
# try a few offsets
for d in (-60, -40, -20, 0, 20, 40, 60):
    p = pos + d
    print('--- off', d, ':', repr(s[p-60:p+60]))
