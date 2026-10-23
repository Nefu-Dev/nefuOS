s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
base = s.index('\n') + 1  # line 1 length + newline
i = base + 25900
print(repr(s[i-200:i+1200]))
