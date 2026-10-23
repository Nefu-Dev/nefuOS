s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
lines = s.split('\n')
base = 0
for i in range(1):
    base += len(lines[i]) + 1
# look backwards from 26014 for the Vj array definition
pos = base + 25900
print(repr(s[pos:base+26100]))
