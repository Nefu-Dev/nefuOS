# -*- coding: utf-8 -*-
# serialize all JS runtime access with a portable spinlock
p = r'D:\mycppos1\nefuOS\core\apps\minijs.cpp'
s = open(p, encoding='utf-8').read()

old = '''int mini_js_run(const char* script, char* out, int out_sz,
                    char* html, int html_sz) {
    if (!script || !out || out_sz <= 1) return 1;
    out[0] = 0;
    ensure_js();'''
new = '''static volatile int s_js_lock = 0;   // serializes quickjs across threads
static inline void js_lock(void) {
    while (__sync_lock_test_and_set(&s_js_lock, 1)) {}
}
static inline void js_unlock(void) {
    __sync_lock_release(&s_js_lock);
}

int mini_js_run(const char* script, char* out, int out_sz,
                    char* html, int html_sz) {
    if (!script || !out || out_sz <= 1) return 1;
    out[0] = 0;
    js_lock();
    ensure_js();'''
print('count:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

old2 = '''    JS_FreeValue(s_ctx, r);
    out[rc.oi] = 0;
    return err;
}

} // namespace nefu'''
new2 = '''    JS_FreeValue(s_ctx, r);
    out[rc.oi] = 0;
    js_unlock();
    return err;
}

} // namespace nefu'''
print('count2:', s.count(old2))
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)

open(p, 'w', encoding='utf-8', newline='').write(s)
print('lock added')
