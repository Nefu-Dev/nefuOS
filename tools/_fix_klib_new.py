# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\klib\klib.h'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''#pragma once
#include <stdint.h>
#include <stddef.h>'''
new = '''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <new>'''
assert s.count(old) == 1, 'inc'
s = s.replace(old, new)

old2 = '''// placement new/delete: 与标准 <new> 的 inline 定义互斥，用 _NEW 守卫避免重复定义
#ifndef _NEW
#define _NEW
inline void* operator new(size_t sz, void* p) noexcept { (void)sz; return p; }
inline void* operator new[](size_t sz, void* p) noexcept { (void)sz; return p; }
inline void  operator delete(void* p, void* place) noexcept { (void)p; (void)place; }
inline void  operator delete[](void* p, void* place) noexcept { (void)p; (void)place; }
#endif'''
new2 = '''// placement new/delete 由标准 <new> 提供（上面已 include），此处不再重复定义'''
assert s.count(old2) == 1, 'placement'
s = s.replace(old2, new2)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK klib.h v2')
