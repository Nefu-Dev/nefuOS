s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
lines = s.split('\n')
base = 0
for i in range(1):
    base += len(lines[i]) + 1
print(repr(s[base+25820:base+25900]))
