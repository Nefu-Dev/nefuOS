# -*- coding: utf-8 -*-
# display layer: free cmd + terminal boot log reflect the 4GB model
import re

# 1) syscmd.cpp cmd_free: 512MB -> 4096MB
p = r'D:\mycppos1\nefuOS\core\termcmds\syscmd.cpp'
s = open(p, encoding='utf-8').read()
old = '''    // 模拟 512MB 物理内存
    const uint32_t total = 512u * 1024u;
    uint32_t used  = 213u * 1024u;
    uint32_t free  = total - used;'''
new = '''    // 逻辑系统内存上限 4GB (内核512MB + 浏览器2GB + 余量1.5GB)
    const uint32_t total = 4096u * 1024u;
    uint32_t used  = (uint32_t)((KERNEL_MEM_BUDGET_MB + 64u) * 1024u);
    uint32_t free  = total - used;'''
print('syscmd count:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)
# need memcfg include
if '#include "memcfg.h"' not in s:
    s = s.replace('#include "syscmd.h"', '#include "syscmd.h"\n#include "memcfg.h"', 1) if '#include "syscmd.h"' in s else s
open(p, 'w', encoding='utf-8', newline='').write(s)
print('syscmd done')

# 2) terminal.cpp boot log "Memory: 64MB available" -> 4096MB
p = r'D:\mycppos1\nefuOS\core\apps\terminal.cpp'
s = open(p, encoding='utf-8').read()
old2 = '        term_print(t, "[    0.001000] Memory: 64MB available");'
new2 = '        term_print(t, "[    0.001000] Memory: 4096MB available");'
print('terminal mem count:', s.count(old2))
if s.count(old2) == 1:
    s = s.replace(old2, new2, 1)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('terminal done')
else:
    print('terminal pattern missing')
