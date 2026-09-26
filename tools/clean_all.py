# -*- coding: utf-8 -*-
# final cleanup: paint scroll klog, key klog, nk/nk2 klogs, KEYTAG printf
import io

# 1) browser.cpp: paint scroll + key klog
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()
old = '''                       int* last_used = 0, int gen = 0, const char* base_url = 0) {
    klogf("browser: paint scroll=%d runs=%d", scroll, (int)lay.runs.size());
    // pass 0: page-level (body/html) background images at the very bottom'''
new = '''                       int* last_used = 0, int gen = 0, const char* base_url = 0) {
    // pass 0: page-level (body/html) background images at the very bottom'''
print('b1:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

old2 = '''    if (e->keycode == KEY_PGDN || e->keycode == KEY_PGUP || e->keycode == KEY_DOWN || e->keycode == KEY_END ||
        e->keycode == KEY_HOME || e->keycode == KEY_ENTER)
        klogf("browser: key %d scroll=%d max=%d focus=%d", e->keycode, tab.scroll, tab.max_scroll, st->input_focus ? 1 : 0);'''
print('b2:', s.count(old2))
if s.count(old2) == 1:
    s = s.replace(old2, '', 1)
open(p, 'w', encoding='utf-8', newline='').write(s)

# 2) nefuos.cpp: nk / nk2 klogs
p2 = r'D:\mycppos1\nefuOS\core\nefuos.cpp'
s2 = open(p2, encoding='utf-8').read()
old3 = '''    klogf("nk key=%d ascii=%d down=%d", keycode, (int)ascii, down ? 1 : 0);
    if (!s_inited || !s_booted) return;'''
new3 = '''    if (!s_inited || !s_booted) return;'''
print('n1:', s2.count(old3))
assert s2.count(old3) == 1
s2 = s2.replace(old3, new3, 1)
old4 = '''    klogf("nk2 dispatch key=%d ascii=%d", keycode, (int)ascii);
    if (desktop_handle_key(keycode, ascii, s_mods, utf8)) return;'''
new4 = '''    if (desktop_handle_key(keycode, ascii, s_mods, utf8)) return;'''
print('n2:', s2.count(old4))
assert s2.count(old4) == 1
s2 = s2.replace(old4, new4, 1)
open(p2, 'w', encoding='utf-8', newline='').write(s2)

# 3) win32.cpp: KEYTAG printf
p3 = r'D:\mycppos1\nefuOS\backends\win32\win32.cpp'
s3 = open(p3, encoding='utf-8').read()
old5 = '''                                printf("[KEYTAG] %s down\\n", tag);
                                nefuos_handle_key(kc2, 0, true);
                                printf("[KEYTAG] %s up\\n", tag);
                                nefuos_handle_key(kc2, 0, false);'''
new5 = '''                                nefuos_handle_key(kc2, 0, true);
                                nefuos_handle_key(kc2, 0, false);'''
print('w1:', s3.count(old5))
assert s3.count(old5) == 1
s3 = s3.replace(old5, new5, 1)
open(p3, 'w', encoding='utf-8', newline='').write(s3)
print('all cleaned')
