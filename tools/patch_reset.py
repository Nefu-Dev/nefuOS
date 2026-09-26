# -*- coding: utf-8 -*-
# add mini_js_reset() to minijs.h + minijs.cpp (both the bare and host impls)
h = r'D:\mycppos1\nefuOS\core\apps\minijs.h'
s = open(h, encoding='utf-8').read()
old = '''void mini_js_set_env(const char* url, const char* title, int w, int h);

} // namespace nefu'''
new = '''void mini_js_set_env(const char* url, const char* title, int w, int h);

// Drop the JS runtime entirely (fresh globals for a new page). Safe to call
// from the main thread between page loads; the next mini_js_run rebuilds it.
void mini_js_reset(void);

} // namespace nefu'''
assert s.count(old) == 1
open(h, 'w', encoding='utf-8', newline='').write(s.replace(old, new, 1))
print('h ok')

c = r'D:\mycppos1\nefuOS\core\apps\minijs.cpp'
s = open(c, encoding='utf-8').read()
# host impl: after mini_js_run 5-arg (with lock)
old2 = '''    JS_FreeValue(s_ctx, r);
    out[rc.oi] = 0;
    js_unlock();
    return err;
}

} // namespace nefu'''
new2 = '''    JS_FreeValue(s_ctx, r);
    out[rc.oi] = 0;
    js_unlock();
    return err;
}

void mini_js_reset(void) {
    js_lock();
    if (s_ctx) { JS_FreeContext(s_ctx); s_ctx = 0; }
    if (s_rt) { JS_FreeRuntime(s_rt); s_rt = 0; }
    js_unlock();
}

} // namespace nefu'''
print('host count:', s.count(old2))
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)

# bare impl (the #else branch): find its mini_js_run 5-arg tail
old3 = '''    out[rc.oi] = 0;
    return err;
}
#endif'''
if old3 in s:
    new3 = '''    out[rc.oi] = 0;
    return err;
}

void mini_js_reset(void) { /* bare JS has no persistent runtime */ }
#endif'''
    print('bare count:', s.count(old3))
    s = s.replace(old3, new3, 1)
else:
    print('bare tail marker not found - checking other marker')
    old3b = '''    return err;
}

} // namespace nefu'''
    if s.count(old3b) == 1 and 'NEFU_BARE' in s:
        new3b = '''    return err;
}

void mini_js_reset(void) { /* bare JS has no persistent runtime */ }
} // namespace nefu'''
        print('bare alt count:', s.count(old3b))
        s = s.replace(old3b, new3b, 1)
    else:
        print('WARN: bare reset not added (count=%d)' % s.count(old3b))

open(c, 'w', encoding='utf-8', newline='').write(s)
print('cpp ok')
