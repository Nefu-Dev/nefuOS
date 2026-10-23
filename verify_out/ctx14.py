s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
start = s.index('(()=>{const jb=atob("RXJyb3I=")')
seg = s[start:start+1200]
print(repr(seg))
