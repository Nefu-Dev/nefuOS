s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
# tail
tail = s[-3000:]
print(repr(tail))
