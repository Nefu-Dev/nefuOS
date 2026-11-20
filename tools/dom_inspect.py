import re, sys
p = sys.argv[1]
needles = sys.argv[2:]
h = open(p, encoding='utf-8', errors='replace').read()
print('len', len(h))
for nd in needles:
    pats = [r'id="%s"' % nd, r"id='%s'" % nd, r'id=%s' % nd]
    n = 0
    for pat in pats:
        for m in re.finditer(pat, h):
            n += 1
            if n <= 2:
                print('== %s at %d' % (nd, m.start()))
                print(re.sub(r'\s+', ' ', h[max(0, m.start()-200):m.start()+900]))
                print('--')
    print('%s hits=%d' % (nd, n))
