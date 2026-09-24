# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()
marker = 'argv);`n    else if (strcmp(a0, "netprototest") ='
assert s.count(marker) == 1, 'stray-marker2'
s = s.replace(marker, 'argv);\n    else if (strcmp(a0, "netprototest") =')
with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK stray2')
