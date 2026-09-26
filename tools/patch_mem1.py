# -*- coding: utf-8 -*-
# Memory budget model: memcfg include + balloc/bfree declarations
p = r'D:\mycppos1\nefuOS\core\platform.h'
s = open(p, encoding='utf-8').read()

old = '''#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {'''
new = '''#pragma once
#include <stdint.h>
#include <stddef.h>
#include "memcfg.h"

namespace nefu {'''
print('platform.h include count:', s.count(old))
s = s.replace(old, new, 1)

old2 = '''// ===================== memory =====================
void* kalloc(size_t sz);
void  kfree(void* p);
void* krealloc(void* p, size_t sz);'''
new2 = '''// ===================== memory =====================
void* kalloc(size_t sz);          // kernel arena (kernel + NVFS + network)
void  kfree(void* p);
void* krealloc(void* p, size_t sz);
void* balloc(size_t sz);          // browser arena (up to BROWSER_MEM_BUDGET_MB)
void  bfree(void* p);
void* brealloc(void* p, size_t sz);'''
print('platform.h balloc count:', s.count(old2))
s = s.replace(old2, new2, 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('platform.h done')
