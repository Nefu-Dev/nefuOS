# -*- coding: utf-8 -*-
import io, sys

p = r'D:\mycppos1\nefuOS\core\apps\term_ext.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

# 1) 修复遗留字面 \n（文件中是反引号+n 两个字符）
old = 'else if (strcmp(a0, "dbtest") == 0) cmd_dbtest(t, argc, argv);' + chr(96) + 'n    else if (strcmp(a0, "mltest") == 0) cmd_mltest(t, argc, argv);'
new = 'else if (strcmp(a0, "dbtest") == 0) cmd_dbtest(t, argc, argv);\n    else if (strcmp(a0, "mltest") == 0) cmd_mltest(t, argc, argv);'
assert s.count(old) == 1, 'stray newline'
s = s.replace(old, new)

# 2) 改名新命令为 complibtest（函数定义处）
old2 = 'static void cmd_comptest(TermState* t, int argc, const char** argv) {\n    (void)argc; (void)argv;\n    // run every compression library self test and report a combined verdict'
new2 = 'static void cmd_complibtest(TermState* t, int argc, const char** argv) {\n    (void)argc; (void)argv;\n    // run every compression library self test and report a combined verdict'
assert s.count(old2) == 1, 'rename fn'
s = s.replace(old2, new2)

# 3) cmdlist 补 complibtest
old3 = '"texttest", "datlibtest", "crypttest", "comptest",'
new3 = '"texttest", "datlibtest", "crypttest", "comptest", "complibtest",'
assert s.count(old3) == 1, 'cmdlist'
s = s.replace(old3, new3)

# 4) dispatch 补 complibtest
old4 = 'else if (strcmp(a0, "comptest") == 0) cmd_comptest(t, argc, argv);'
if s.count(old4) >= 1:
    s = s.replace(old4, old4 + '\n    else if (strcmp(a0, "complibtest") == 0) cmd_complibtest(t, argc, argv);', 1)

# 5) Boundary::Wrap -> Torus
old5 = 'simulate::Boundary::Wrap'
new5 = 'simulate::Boundary::Torus'
assert s.count(old5) >= 1, 'wrap'
s = s.replace(old5, new5)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK all patches')
