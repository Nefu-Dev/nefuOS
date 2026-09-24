# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

# 清掉所有 "`n"（反引号+n）字面残留：反引号是用户粘贴时被转义产生的
count = 0
while ';`n' in s:
    s = s.replace(';`n', ';\n', 1)
    count += 1
with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('fixed stray count =', count)
