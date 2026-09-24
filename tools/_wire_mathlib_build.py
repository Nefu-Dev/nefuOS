# -*- coding: utf-8 -*-
import io

math_block = '''    "core\\mathlib\\bigint.cpp", "core\\mathlib\\rational.cpp", "core\\mathlib\\complex.cpp",
    "core\\mathlib\\matrix.cpp", "core\\mathlib\\fft.cpp", "core\\mathlib\\polynomial.cpp",
    "core\\mathlib\\stat.cpp", "core\\mathlib\\regression.cpp", "core\\mathlib\\prime.cpp",
    "core\\mathlib\\random.cpp", "core\\mathlib\\vector2.cpp", "core\\mathlib\\gcd.cpp",
'''

p = r'D:\mycppos1\nefuOS\tools\build_iso.ps1'
with io.open(p, encoding='utf-8') as f: s = f.read()
old = '    "core\\complib\\bwt.cpp",\n'
assert s.count(old) == 1, p
s = s.replace(old, old + math_block)
with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK', p)
