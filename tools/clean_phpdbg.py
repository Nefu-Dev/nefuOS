# -*- coding: utf-8 -*-
import re
p = r'D:\mycppos1\nefuOS\core\apps\php2html.h'
s = open(p, encoding='utf-8').read()
orig = s

pairs = [
    ('    nefu::klogf("php src %.60s", src);\n', ''),
    ('        nefu::klogf("php scan p=[%.50s]", p);\n', ''),
    ('        nefu::klogf("php arr nargs=%d out=[%.80s]", e->nargs, S->b);\n', ''),
    ('        nefu::klogf("php count arr=[%.60s]", arr);\n', ''),
    ('        nefu::klogf("php foreach arr=[%.90s] kv=[%s] nm=[%s]", arr.s ? arr.s : "(null)", st->kv, st->name);\n', ''),
    ('''    { FILE* dbg = fopen("php_conv_dump.txt", "wb");
      if (dbg) { fwrite(out, 1, (size_t)(V.outn < 2000 ? V.outn : 2000), dbg); fclose(dbg); } }
''', ''),
    ('    nefu::klogf("php out=[%.160s]", out);\n', ''),
]
for old, new in pairs:
    n = s.count(old)
    print(repr(old[:50]), '->', n)
    s = s.replace(old, new)

open(p, 'w', encoding='utf-8', newline='').write(s)
print('removed', orig.count('klogf("php'), '->', s.count('klogf("php'))
