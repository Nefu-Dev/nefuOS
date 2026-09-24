# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = 'cmd_mltest(t, argc, argv);`n    else if (strcmp(a0, "termcmdstest") == 0) cmd_termcmdstest(t, argc, argv);`n    else if (strcmp(a0, "minilangtest") == 0) cmd_minilangtest(t, argc, argv);'
new = 'cmd_mltest(t, argc, argv);\n    else if (strcmp(a0, "termcmdstest") == 0) cmd_termcmdstest(t, argc, argv);\n    else if (strcmp(a0, "minilangtest") == 0) cmd_minilangtest(t, argc, argv);'
assert s.count(old) == 1, 'stray'
s = s.replace(old, new)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK stray fixed')
