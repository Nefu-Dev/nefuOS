# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\apps\terminal.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''            term_print(t, "  - complib/comp_all.h (bitio/rle/huffman/lz77/lzw/arithmetic/bwt)");'''
new = '''            term_print(t, "  - complib/comp_all.h (bitio/rle/huffman/lz77/lzw/arithmetic/bwt)");
            term_print(t, "  - mathlib/math_all.h (bigint/rational/complex/matrix/fft/poly/stat/regress/prime/rand/vec2/gcd)");'''
assert s.count(old) == 1, 'help1'
s = s.replace(old, new)

old2 = '''            term_print(t, "  - including complib/comp_all.h");'''
new2 = '''            term_print(t, "  - including complib/comp_all.h");
            term_print(t, "  - including mathlib/math_all.h");'''
assert s.count(old2) == 1, 'help2'
s = s.replace(old2, new2)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK terminal.cpp')
