# -*- coding: utf-8 -*-
# Integrate PHP transpiler into browser.cpp
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()

def rep(old, new, tag):
    global s
    c = s.count(old)
    if c != 1:
        raise SystemExit('FAIL %s count=%d for %s' % (tag, c, old[:90]))
    s = s.replace(old, new, 1)

# 1) include
rep('''#include "../platform.h"
''',
'''#include "../platform.h"
#include "php2html.h"   // nfuphp::php_convert — PHP template mini-transpiler
''', 'include')

# 2) LoadJob fields
rep('''    char js_out[4096];   // mini_js_run console output (accumulated)
    CSSRule css[CSS_MAX];      // parsed <style> sheet''',
'''    char js_out[4096];   // mini_js_run console output (accumulated)
    bool php_mode;       // page is PHP: body buffered, converted after download
    char* php_buf;       // buffered PHP source (browser arena)
    int php_len, php_cap;
    CSSRule css[CSS_MAX];      // parsed <style> sheet''', 'ljob')

# 3) start_job init
rep('''    j->tab = &tab;
    j->js_err = false;
    j->ok = false;''',
'''    j->tab = &tab;
    j->js_err = false;
    j->php_mode = false;
    j->php_buf = 0;
    j->php_len = 0;
    j->php_cap = 0;
    j->ok = false;''', 'init')

# 4) on_http_chunk
rep('''static bool on_http_chunk(void* ctx, const uint8_t* data, uint32_t len) {
    LoadJob* j = (LoadJob*)ctx;
    if (j->cancelled) { j->aborted = 1; return false; }
    PageParser* pp = (PageParser*)j->parser;
    if (!pp) { j->aborted = 1; return false; }
    pp->feed((const char*)data, (int)len, false);
    if (pp->dom.overflow) { j->aborted = 1; return false; }   // DOM capped
    return true;
}''',
'''static bool on_http_chunk(void* ctx, const uint8_t* data, uint32_t len) {
    LoadJob* j = (LoadJob*)ctx;
    if (j->cancelled) { j->aborted = 1; return false; }
    if (!j->php_mode && j->php_len == 0) {
        // PHP detection: URL carries a .php path, or the very first bytes open
        // a PHP tag. HTML pages keep the normal streaming path untouched.
        if (strstr(j->url, ".php") ||
            (len >= 4 && memcmp(data, "<?ph", 4) == 0) ||
            (len >= 3 && (memcmp(data, "<?=", 3) == 0 || memcmp(data, "<? ", 3) == 0)))
            j->php_mode = true;
    }
    if (j->php_mode) {
        // buffer the whole body; the transpiler needs the complete document
        // (PHP blocks cross chunk boundaries)
        if (j->php_len + (int)len + 1 > j->php_cap) {
            int ncap = j->php_cap ? j->php_cap * 2 : 65536;
            while (ncap < j->php_len + (int)len + 1) ncap *= 2;
            char* nb = (char*)brealloc(j->php_buf, (size_t)ncap);
            if (!nb) { j->aborted = 1; return false; }
            j->php_buf = nb;
            j->php_cap = ncap;
        }
        memcpy(j->php_buf + j->php_len, data, (size_t)len);
        j->php_len += (int)len;
        j->php_buf[j->php_len] = 0;
        return true;
    }
    PageParser* pp = (PageParser*)j->parser;
    if (!pp) { j->aborted = 1; return false; }
    pp->feed((const char*)data, (int)len, false);
    if (pp->dom.overflow) { j->aborted = 1; return false; }   // DOM capped
    return true;
}''', 'chunk')

# 5) load_worker feed: PHP conversion
rep('''    if (!j->cancelled && (ok || j->aborted)) {
        pp.feed(0, 0, true);   // final flush of any held-back tail
        j->phase = 1;''',
'''    if (!j->cancelled && (ok || j->aborted)) {
        if (j->php_mode && j->php_buf) {
            // PHP template: run the mini transpiler, then parse the emitted
            // HTML (scripts inside it still run through the mini JS engine).
            const int conv_cap = 8 * 1024 * 1024;
            char* conv = (char*)balloc((size_t)conv_cap);
            if (conv) {
                int clen = nfuphp::php_convert(j->php_buf, conv, conv_cap);
                klogf("browser: php converted %d -> %d bytes", j->php_len, clen);
                pp.feed(conv, clen, true);
                bfree(conv);
            }
        } else {
            pp.feed(0, 0, true);   // final flush of any held-back tail
        }
        j->phase = 1;''', 'feed')

# 6) frees: dtor
rep('''            if (jk->data) kfree(jk->data);
            if (jk->dom) delete jk->dom;''',
'''            if (jk->data) kfree(jk->data);
            if (jk->php_buf) bfree(jk->php_buf);
            if (jk->dom) delete jk->dom;''', 'dtor')

# 7) frees: retire
rep('''        if (tab.job_keep->data) kfree(tab.job_keep->data);
        if (tab.job_keep->dom) delete tab.job_keep->dom;''',
'''        if (tab.job_keep->data) kfree(tab.job_keep->data);
        if (tab.job_keep->php_buf) bfree(tab.job_keep->php_buf);
        if (tab.job_keep->dom) delete tab.job_keep->dom;''', 'retire')

# 8) frees: poll_jobs
rep('''        if (j->data) kfree(j->data);
        if (j->dom) delete j->dom;''',
'''        if (j->data) kfree(j->data);
        if (j->php_buf) bfree(j->php_buf);
        if (j->dom) delete j->dom;''', 'poll')

open(p, 'w', encoding='utf-8', newline='').write(s)
print('browser.cpp php integration ok')
