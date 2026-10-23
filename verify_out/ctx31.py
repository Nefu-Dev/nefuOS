s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
nl = s.index('\n')
pos = nl + 1 + 271554
# find module boundaries before pos
import re
heads = [m.start() for m in re.finditer(r'\(jb,bj,Oj\)=>\{', s[:pos])]
print('num heads before:', len(heads))
if heads:
    h = heads[-1]
    print('module head at', h, ':', repr(s[h:h+120]))
    # module body end: find matching close? hard. print region after head head+200..head+400
    print('after head:', repr(s[h+200:h+400]))
