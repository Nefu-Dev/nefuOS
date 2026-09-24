# -*- coding: utf-8 -*-
import sys

def patch(p, old, new):
    raw = open(p, 'rb').read()
    bom = raw[:3] == b'\xef\xbb\xbf'
    s = raw.decode('utf-8-sig')
    n = s.count(old)
    if n != 1:
        raise SystemExit('NOT UNIQUE (%d) in %s: %r' % (n, p, old[:60]))
    s = s.replace(old, new)
    data = s if not bom else '\ufeff' + s
    open(p, 'w', encoding='utf-8', newline='').write(data)
    print('OK %s' % p)

add = ('  "core\\gfxlib\\noise.cpp", "core\\gfxlib\\color.cpp", "core\\apps\\gfxlab.cpp",\n'
       '  "core\\textlib\\levenshtein.cpp", "core\\textlib\\lcs.cpp", "core\\textlib\\kmp.cpp",\n'
       '  "core\\textlib\\aho.cpp", "core\\textlib\\token.cpp", "core\\textlib\\ngram.cpp",\n'
       '  "core\\textlib\\regexlite.cpp", "core\\textlib\\diff.cpp",')

for p in (r'D:\mycppos1\nefuOS\tools\build_host_only.ps1',
          r'D:\mycppos1\nefuOS\tools\build_iso.ps1'):
    old = ('  "core\\gfxlib\\noise.cpp", "core\\gfxlib\\color.cpp", "core\\apps\\gfxlab.cpp",')
    patch(p, old, add)
