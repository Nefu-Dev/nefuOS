# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\mathlib\bigint.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''        if (rem.len >= CAP) return *this;   // 防御：超长
        rem.d[rem.len] = num.d[i];
        rem.len = rem.len + 1;
        rem.trim_zeros();'''
new = '''        if (rem.len >= CAP) return *this;   // 防御：超长
        // rem = rem*10 + 位：整体右移一格，新位放个位
        for (int k = rem.len; k > 0; k--) rem.d[k] = rem.d[k - 1];
        rem.d[0] = num.d[i];
        rem.len = rem.len + 1;
        rem.trim_zeros();'''
assert s.count(old) == 1, 'append'
s = s.replace(old, new)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK')
