import re
s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
# module format: 67:[function(jb,bj,Oj){ ... return [...]
m = re.search(r'67:\[function', s)
if m:
    i = m.start()
    # find the return array inside, look for atob calls and array literals
    print(repr(s[i:i+1500]))
