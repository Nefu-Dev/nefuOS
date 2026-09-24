# -*- coding: utf-8 -*-
import re
p = r'D:\mycppos1\nefuOS\core\apps\terminal.cpp'
raw = open(p, 'rb').read()
bom = raw[:3] == b'\xef\xbb\xbf'
s = raw.decode('utf-8-sig')

def rep(old, new, count=1):
    global s
    n = s.count(old)
    if n < count:
        raise SystemExit('NOT FOUND (%d): %r' % (n, old[:60]))
    s = s.replace(old, new, count)

# 帮助文本区（usage 分支）
rep('gfxlib/gfxlib_all.h (raster/geo/transform/noise/color)");',
    'gfxlib/gfxlib_all.h (raster/geo/transform/noise/color)");\n            term_print(t, "  - textlib/text_all.h (levenshtein/lcs/kmp/aho/token/ngram/regex/diff)");')

# 编译过程区（else 分支）
rep('including gfxlib/gfxlib_all.h");',
    'including gfxlib/gfxlib_all.h");\n            term_print(t, "  - including textlib/text_all.h");')

data = s if not bom else '\ufeff' + s
open(p, 'w', encoding='utf-8', newline='').write(data)
print('OK bom=%s size=%d' % (bom, len(data)))
