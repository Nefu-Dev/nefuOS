# -*- coding: utf-8 -*-
# browser.cpp: MAX_PAGE 2GB + big buffers move to balloc (browser arena)
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()

def rep(old, new, tag, n=1):
    global s
    c = s.count(old)
    print(tag, 'count:', c)
    assert c == n, (tag, c)
    s = s.replace(old, new, n)

# 1) MAX_PAGE -> 2GB (browser budget)
rep('const int MAX_PAGE  = 200 * 1024 * 1024;  // page buffer cap (200 MB per tab)',
    'const int MAX_PAGE  = 2048 * 1024 * 1024;  // page buffer cap (2 GB per tab, browser arena)',
    'max_page')

# 2) sstream -> balloc (page buffer lives in the browser arena)
rep('            sstream = (char*)kalloc((size_t)scap);',
    '            sstream = (char*)balloc((size_t)scap);', 'sstream_alloc')
rep('            char* nb = (char*)kalloc((size_t)scap);',
    '            char* nb = (char*)balloc((size_t)scap);', 'sstream_grow')
rep('            kfree(sstream);',
    '            bfree(sstream);', 'sstream_free')

# 3) worker tail: release the page buffer (was leaked; 2GB cap makes it matter)
rep('''        }
    }
    j->done = 1;
}''',
    '''        }
    }
    if (pp.sstream) { bfree(pp.sstream); pp.sstream = 0; }
    j->done = 1;
}''', 'sstream_tail_free')

# 4) js_html -> balloc
rep('            j->js_html = (char*)kalloc((size_t)j->js_html_sz);',
    '            j->js_html = (char*)balloc((size_t)j->js_html_sz);', 'js_html_alloc')

# 5) canvas readback fb -> balloc
rep('''        if (c.fb) kfree(c.fb);
        c.fb = 0;
        c.fbw = c.fbh = 0;
        uint8_t* nb = (uint8_t*)kalloc((size_t)c.w * (size_t)c.h * 4);''',
    '''        if (c.fb) bfree(c.fb);
        c.fb = 0;
        c.fbw = c.fbh = 0;
        uint8_t* nb = (uint8_t*)balloc((size_t)c.w * (size_t)c.h * 4);''', 'canvas_fb')

# 6) texture upload px -> balloc
rep('''    if (s_texp[slot].px) kfree(s_texp[slot].px);
    s_texp[slot].px = (uint8_t*)kalloc(len);''',
    '''    if (s_texp[slot].px) bfree(s_texp[slot].px);
    s_texp[slot].px = (uint8_t*)balloc(len);''', 'tex_alloc')
rep('''                    kfree(s_texp[i].px);
                    s_texp[i].px = 0; s_texp[i].len = 0; s_texp[i].cid[0] = 0;''',
    '''                    bfree(s_texp[i].px);
                    s_texp[i].px = 0; s_texp[i].len = 0; s_texp[i].cid[0] = 0;''', 'tex_free')

# 7) poll_jobs js_html free -> bfree
rep('''            commit_page(tab, j);   // consumes j->js_html (DOM shim re-render)
            if (j->js_html) { kfree(j->js_html); j->js_html = 0; }''',
    '''            commit_page(tab, j);   // consumes j->js_html (DOM shim re-render)
            if (j->js_html) { bfree(j->js_html); j->js_html = 0; }''', 'poll_free1')
rep('''            if (j->js_html) { kfree(j->js_html); j->js_html = 0; }''',
    '''            if (j->js_html) { bfree(j->js_html); j->js_html = 0; }''', 'poll_free2')

open(p, 'w', encoding='utf-8', newline='').write(s)
print('browser mem patch done')
