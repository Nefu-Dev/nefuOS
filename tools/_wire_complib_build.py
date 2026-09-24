# -*- coding: utf-8 -*-
import io, sys

def patch(path, old, new, tag):
    with io.open(path, encoding='utf-8') as f: s = f.read()
    c = s.count(old)
    if c != 1:
        print(f'FAIL[{tag}] count={c} in {path}')
        sys.exit(1)
    s = s.replace(old, new)
    with io.open(path, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
    print(f'OK[{tag}] {path}')

for p, tag in [(r'D:\mycppos1\nefuOS\tools\build_host_only.ps1', 'host'),
               (r'D:\mycppos1\nefuOS\tools\build_iso.ps1', 'iso')]:
    try:
        with io.open(p, encoding='utf-8') as f: s = f.read()
    except IOError:
        print(f'SKIP {p} not found')
        continue
    old = '    "core\\cryptlib\\xor.cpp",\n'
    new = ('    "core\\cryptlib\\xor.cpp",\n'
           '    "core\\complib\\bitio.cpp", "core\\complib\\rle.cpp", "core\\complib\\huffman.cpp",\n'
           '    "core\\complib\\lz77.cpp", "core\\complib\\lzw.cpp", "core\\complib\\arithmetic.cpp",\n'
           '    "core\\complib\\bwt.cpp",\n')
    if s.count(old) != 1:
        print(f'FAIL[{tag}] anchor count={s.count(old)}')
        continue
    s = s.replace(old, new)
    with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
    print(f'OK[{tag}] {p}')
