# -*- coding: utf-8 -*-
# Event round-trip: shim registry + data-nfu + __nfuDispatch; browser click
# dispatch + per-tab js_html re-render; virtual-scroll culling in paint.

# ---------- 1) jsdom_shim.h ----------
p = r'D:\mycppos1\nefuOS\core\apps\jsdom_shim.h'
s = open(p, encoding='utf-8').read()

old = '''"  function Node(tag){\\n"
"    this._tag = tag; this._parent = null; this._children = [];\\n"
"    this._attrs = {}; this._style = {}; this._events = {};\\n"
"    this._text = ''; this._inner = null;\\n"
"  }\\n"'''
new = '''"  var seq = 0, _all = [];\\n"
"  function Node(tag){\\n"
"    this._tag = tag; this._parent = null; this._children = [];\\n"
"    this._attrs = {}; this._style = {}; this._events = {};\\n"
"    this._text = ''; this._inner = null;\\n"
"    this._nfuIdx = ++seq; _all[this._nfuIdx] = this;\\n"
"  }\\n"'''
print('shim node ctor:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

old2 = '''"    out.push('<' + t);\\n"
"    for (var k in n._attrs){ var v = n._attrs[k]; if (v !== undefined && v !== null) out.push(' ' + k + '=\\"' + esc(v) + '\\"'); }\\n"'''
new2 = '''"    out.push('<' + t);\\n"
"    out.push(' data-nfu=\\"' + n._nfuIdx + '\\"');\\n"
"    for (var k in n._attrs){ var v = n._attrs[k]; if (v !== undefined && v !== null) out.push(' ' + k + '=\\"' + esc(v) + '\\"'); }\\n"'''
print('shim serialize:', s.count(old2))
assert s.count(old2) == 1
s = s.replace(old2, new2, 1)

old3 = '''"  g.requestAnimationFrame = function(f){ if (f) f(0); return 0; };\\n"
"  g.cancelAnimationFrame = function(){};\\n"
"  if (g.window) g.window.document = nd;\\n"'''
new3 = '''"  g.requestAnimationFrame = function(f){ if (f) f(0); return 0; };\\n"
"  g.cancelAnimationFrame = function(){};\\n"
"  // click event round-trip: the browser hit-tests the serialized DOM and\n"
"  // calls __nfuDispatch(idx) with the data-nfu index of the element under\n"
"  // the pointer; registered listeners mutate the tree, dirty() re-serializes.\n"
"  g.__nfuDispatch = function(idx, x, y){\\n"
"    var n = _all[idx];\\n"
"    if (!n) return;\\n"
"    var ev = { type: 'click', target: n, clientX: x || 0, clientY: y || 0 },\\n"
"        a = (n._events && n._events.click) || [], i;\\n"
"    for (i = 0; i < a.length; i++){ try { a[i].call(n, ev); } catch(e){} }\\n"
"  };\\n"
"  if (g.window) g.window.document = nd;\\n"'''
print('shim dispatch:', s.count(old3))
assert s.count(old3) == 1
s = s.replace(old3, new3, 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('jsdom_shim.h done')

# ---------- 2) browser.cpp ----------
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()

# 2a) DomNode: nfu_idx field
old = '''    CSSRule inl;       // inline style="" (selector unused)
    bool has_inl;
    DomNode() : has_inl(false), cw(300), ch(150) { cls[0] = id[0] = onclick[0] = tag_name[0] = 0; }'''
new = '''    CSSRule inl;       // inline style="" (selector unused)
    bool has_inl;
    int nfu_idx;       // data-nfu index (JS DOM shim element), -1 = none
    DomNode() : has_inl(false), cw(300), ch(150), nfu_idx(-1)
        { cls[0] = id[0] = onclick[0] = tag_name[0] = 0; }'''
print('domnode field:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2b) attribute loop: extract data-nfu
old = '''                else if (ci_eq(aname, "width")) cw = atoi(aval);
                else if (ci_eq(aname, "height")) ch = atoi(aval);'''
new = '''                else if (ci_eq(aname, "width")) cw = atoi(aval);
                else if (ci_eq(aname, "height")) ch = atoi(aval);
                else if (ci_eq(aname, "data-nfu")) nfu = atoi(aval);'''
print('attr nfu:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2c) declare nfu local next to other attr locals
old = '''            // attributes
            char cls[48] = {0}, id[32] = {0}, onclick[96] = {0};
            int cw = 300, ch = 150;'''
new = '''            // attributes
            char cls[48] = {0}, id[32] = {0}, onclick[96] = {0};
            int cw = 300, ch = 150, nfu = -1;'''
print('attr nfu decl:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2d) apply nfu to the opened node (void + normal open sites)
old = '''                if (t == Tag::Br || t == Tag::Hr || t == Tag::Img) {
                    dom.open(t, href, src, alt, cls, id, has_inl ? &inl : 0, onclick, tagbuf);
                    dom.close_top();
                }'''
new = '''                if (t == Tag::Br || t == Tag::Hr || t == Tag::Img) {
                    dom.open(t, href, src, alt, cls, id, has_inl ? &inl : 0, onclick, tagbuf);
                    if (nfu >= 0) dom.nodes[dom.top()].nfu_idx = nfu;
                    dom.close_top();
                }'''
print('nfu void:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

old = '''            dom.open(t, href, src, alt, cls, id, has_inl ? &inl : 0, onclick, tagbuf);
            if (t == Tag::Canvas) {
                DomNode& cn = dom.nodes[dom.top()];
                cn.cw = cw; cn.ch = ch;
            }'''
new = '''            dom.open(t, href, src, alt, cls, id, has_inl ? &inl : 0, onclick, tagbuf);
            if (nfu >= 0) dom.nodes[dom.top()].nfu_idx = nfu;
            if (t == Tag::Canvas) {
                DomNode& cn = dom.nodes[dom.top()];
                cn.cw = cw; cn.ch = ch;
            }'''
print('nfu normal:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2e) TabState: per-tab js_html + pending click
old = '''    char js_msg[160];     // last JS console output (shown in the status bar)'''
new = '''    char js_msg[160];     // last JS console output (shown in the status bar)
    char* js_html;        // DOM shim re-sync buffer (event re-render)
    int js_html_sz;
    int pending_nfu;      // JS element under a click (data-nfu index), -1 none
    bool has_pending_nfu;'''
print('tabstate fields:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2f) click dispatch in on_mouse (after onclick, before link nav)
old = '''            if (r.node >= 0 && r.node < tab.dom.size()) {
                const char* oc = tab.dom[r.node].onclick;
                if (oc[0]) { exec_onclick(oc); return; }
            }
            if (!r.link) continue;'''
new = '''            if (r.node >= 0 && r.node < tab.dom.size()) {
                const char* oc = tab.dom[r.node].onclick;
                if (oc[0]) { exec_onclick(oc); return; }
                if (tab.dom[r.node].nfu_idx >= 0) {
                    // JS-built element: dispatch its click listeners via the
                    // DOM shim, which re-serializes -> re-render next frame
                    tab.pending_nfu = tab.dom[r.node].nfu_idx;
                    tab.has_pending_nfu = true;
                    return;
                }
            }
            if (!r.link) continue;'''
print('click dispatch:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2g) event processing in on_paint (right after poll_jobs)
old = '''    poll_jobs(st);   // commit finished loads (also starts queued ones)

    TabState& tab = st->tabs[st->active];'''
new = '''    poll_jobs(st);   // commit finished loads (also starts queued ones)

    TabState& tab = st->tabs[st->active];

    // ---- JS click event round-trip: dispatch to the DOM shim, re-parse the
    // serialized tree if the listeners mutated it, and re-layout ----
    if (tab.has_pending_nfu) {
        tab.has_pending_nfu = false;
        if (!tab.js_html) {
            tab.js_html_sz = 256 * 1024;
            tab.js_html = (char*)balloc((size_t)tab.js_html_sz);
        }
        char ev[96], out[JS_OUT_MAX];
        out[0] = 0;
        ksprintf(ev, sizeof(ev), "__nfuDispatch(%d,0,0);", tab.pending_nfu);
        nefu::mini_js_set_env(tab.url, tab.title, 0, 0);
        mini_js_run(ev, out, (int)sizeof(out), tab.js_html, tab.js_html_sz);
        if (out[0]) {
            apply_js_commands(tab, out);
            char vis[JS_OUT_MAX]; int vn = 0;
            for (const char* c = out; *c && vn < JS_OUT_MAX - 1; ) {
                if (*c == '\\x01') { while (*c && *c != '\\n') c++; if (*c) c++; continue; }
                vis[vn++] = *c++;
            }
            vis[vn] = 0;
            if (vis[0]) klogf("browser: js: %s", vis);
        }
        if (tab.js_html && tab.js_html[0]) {
            CSSRule css2[CSS_MAX]; int css2_n = 0;
            PageParser pp2(0, 0, css2, &css2_n, CSS_MAX);
            pp2.feed(tab.js_html, (int)strlen(tab.js_html), true);
            if (!pp2.dom.overflow) {
                tab.dom = pp2.dom.nodes;
                klogf("browser: js dom re-render (%d nodes) via click", tab.dom.size());
            }
        }
        tab.layout_w = -1;
    }'''
print('on_paint events:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2h) page navigation: drop the old tab js_html (JS state belongs to that page)
old = '''static void commit_page(TabState& tab, LoadJob* j) {
    clear_imgs(tab);   // previous page's images are gone'''
new = '''static void commit_page(TabState& tab, LoadJob* j) {
    clear_imgs(tab);   // previous page's images are gone
    if (tab.js_html) { bfree(tab.js_html); tab.js_html = 0; }   // old JS state'''
print('commit_page clear:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2i) close_tab: free tab js_html
old = '''        for (int i = 0; i < IMG_MAX; i++) img_last_used[i] = 0;'''
new = '''        if (t.js_html) { bfree(t.js_html); t.js_html = 0; }
        for (int i = 0; i < IMG_MAX; i++) img_last_used[i] = 0;'''
print('close tab clear:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

# 2j) virtual scroll: early-exit paint passes once runs pass the viewport
#     (runs are emitted in document order, y is monotonic non-decreasing)
old = '''    for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        if (r.kind != 3) continue;
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) continue;'''
new = '''    for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        if (r.kind != 3) continue;
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) { if (sy > vh) break; continue; }'''
print('vscroll pass1:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

old = '''        const Run& r = lay.runs[i];
        if (r.kind != 4 || r.full_page) continue;
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) continue;
        if (r.img_id >= 0 && imgs && imgs[r.img_id].done && imgs[r.img_id].rgba) {'''
new = '''        const Run& r = lay.runs[i];
        if (r.kind != 4 || r.full_page) continue;
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) { if (sy > vh) break; continue; }
        if (r.img_id >= 0 && imgs && imgs[r.img_id].done && imgs[r.img_id].rgba) {'''
print('vscroll pass15:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

old = '''    for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) continue;   // vertical culling

        if (r.kind == 2) {   // horizontal rule'''
new = '''    for (int i = 0; i < lay.runs.size(); i++) {
        const Run& r = lay.runs[i];
        int sy = r.y - scroll;
        if (sy + r.h < 0 || sy > vh) { if (sy > vh) break; continue; }   // vertical culling

        if (r.kind == 2) {   // horizontal rule'''
print('vscroll main:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)

open(p, 'w', encoding='utf-8', newline='').write(s)
print('browser.cpp event + vscroll done')
