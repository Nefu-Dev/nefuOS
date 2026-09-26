# -*- coding: utf-8 -*-
# SPA fallback panel: when the page's JS failed (our mini engine can't run a
# production bundle) and there is no visible text, show a status panel with
# the site name instead of a blank viewport.
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()

def rep(old, new, tag='x'):
    global s
    c = s.count(old)
    if c != 1:
        raise SystemExit('FAIL %s count=%d for %s' % (tag, c, old[:70]))
    s = s.replace(old, new, 1)

# 1) LoadJob field
rep('''    volatile int worker_done; // 1 = worker thread has fully exited
    TabState* tab;''',
'''    volatile int worker_done; // 1 = worker thread has fully exited
    TabState* tab;
    bool js_err;       // JS execution failed (mini engine limits / SPA bundle)''', 'ljob')

rep('''    j->committed = 0;
    j->worker_done = 0;
    j->tab = &tab;
    j->ok = false;''',
'''    j->committed = 0;
    j->worker_done = 0;
    j->tab = &tab;
    j->js_err = false;
    j->ok = false;''')

# 2) worker: flag JS failures (external + inline runs)
rep('''                    nefu::mini_js_run(js, jsacc + jan, (int)sizeof(jsacc) - jan - 1,
                                      j->js_html, j->js_html_sz);''',
'''                    j->js_err |= (nefu::mini_js_run(js, jsacc + jan, (int)sizeof(jsacc) - jan - 1,
                                      j->js_html, j->js_html_sz) != 0);''')

rep('''            nefu::mini_js_run(j->script, jsacc + jan, (int)sizeof(jsacc) - jan - 1,
                              j->js_html, j->js_html_sz);''',
'''            j->js_err |= (nefu::mini_js_run(j->script, jsacc + jan, (int)sizeof(jsacc) - jan - 1,
                              j->js_html, j->js_html_sz) != 0);''')

# 3) TabState field
rep('''    bool has_pending_nfu;
    int img_gen;''',
'''    bool has_pending_nfu;
    bool js_err;
    int img_gen;''')

rep('''          pending_nfu(-1), has_pending_nfu(false), js_ev_pending(0),''',
'''          pending_nfu(-1), has_pending_nfu(false), js_err(false), js_ev_pending(0),''')

# 4) commit: propagate
rep('''            tab.css_n = j->css_n;
            for (int k = 0; k < j->css_n && k < CSS_MAX; k++) tab.css[k] = j->css[k];''',
'''            tab.css_n = j->css_n;
            for (int k = 0; k < j->css_n && k < CSS_MAX; k++) tab.css[k] = j->css[k];
            tab.js_err = j->js_err;''')

# 5) paint: fallback panel instead of blank viewport
old5 = '''    tab.img_gen++;
    paint_page(*s, tab.layout, tab.scroll, vx, vy, vw, vh, tab.imgs,
               tab.img_last_used, tab.img_gen, tab.url);'''
new5 = '''    tab.img_gen++;
    if (tab.js_err && tab.loaded && !tab.is_home) {
        bool has_text = false;
        for (int i = 0; i < tab.layout.runs.size() && !has_text; i++) {
            const Run& r = tab.layout.runs[i];
            if (r.kind == 0 && r.text.len() > 0) has_text = true;
        }
        if (!has_text) {
            // SPA / heavy-JS site that the mini engine cannot run: show a
            // friendly panel with the site name instead of a blank viewport.
            int pw = 440, ph = 96;
            int px = vx + (vw - pw) / 2, py = vy + vh / 2 - ph / 2;
            gfx::fillrect(*s, px, py, pw, ph, 0xFF111827);
            gfx::rect(*s, px, py, pw, ph, C_HRLINE);
            const char* l1 = "This site needs a full JavaScript engine";
            char l2[64];
            url_host(tab.url, l2, sizeof(l2));
            char l3[64];
            if (tab.title[0]) {
                int k = 0;
                for (; k < (int)sizeof(l3) - 1 && tab.title[k]; k++)
                    l3[k] = ((unsigned char)tab.title[k] < 128) ? tab.title[k] : '?';
                l3[k] = 0;
            } else l3[0] = 0;
            int ty = py + 26;
            int x0 = px + pw / 2 - (int)strlen(l1) * 4;
            if (x0 < px + 6) x0 = px + 6;
            for (int c = 0; l1[c]; c++)
                gfx::char8x16(*s, x0 + c * 8, ty, l1[c], C_WHITE, 0xFF111827);
            int l2n = (int)strlen(l2);
            if (l2n > 50) l2n = 50;
            x0 = px + pw / 2 - l2n * 4;
            if (x0 < px + 6) x0 = px + 6;
            for (int c = 0; c < l2n; c++)
                gfx::char8x16(*s, x0 + c * 8, ty + 20, l2[c], 0xFF60A5FA, 0xFF111827);
            if (l3[0]) {
                int l3n = (int)strlen(l3);
                if (l3n > 50) l3n = 50;
                x0 = px + pw / 2 - l3n * 4;
                if (x0 < px + 6) x0 = px + 6;
                for (int c = 0; c < l3n; c++)
                    gfx::char8x16(*s, x0 + c * 8, ty + 40, l3[c], C_GRAY, 0xFF111827);
            }
        } else {
            paint_page(*s, tab.layout, tab.scroll, vx, vy, vw, vh, tab.imgs,
                       tab.img_last_used, tab.img_gen, tab.url);
        }
    } else {
        paint_page(*s, tab.layout, tab.scroll, vx, vy, vw, vh, tab.imgs,
                   tab.img_last_used, tab.img_gen, tab.url);
    }'''
rep(old5, new5, 'paint fallback')

open(p, 'w', encoding='utf-8', newline='').write(s)
print('SPA fallback added')
