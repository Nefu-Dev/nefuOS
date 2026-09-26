# -*- coding: utf-8 -*-
# (1) move serializer after DomArena definition
# (2) minijs.cpp second set_env site (active one, with the comment anchor)
# (3) jsdom_shim.h anchor with C-string quoting
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()

# remove the prematurely placed serializer block (from the marker comment to
# the is_block_tag line)
start_m = '// serialize the parsed DOM back to HTML; used to seed the JS DOM shim so'
end_m = 'static bool is_block_tag(Tag t) {'
si = s.find(start_m)
ei = s.find(end_m)
if si >= 0 and ei > si:
    block = s[si:ei]
    s = s[:si] + s[ei:]
    print('removed early serializer', len(block))
else:
    raise SystemExit('early serializer not found for removal')

# insert after DomArena struct closing (find '};' after DomArena ctor end)
anchor = '''    int top() const { return stack.size() > 0 ? stack[stack.size() - 1] : 0; }'''
# DomArena ends after the open()... — find the struct end: search for the
# "open(" signature block end. Simpler: insert right after the struct's
# closing brace which is the line after the ctor list — locate by unique string:
anchor2 = '''    void open(Tag t, const char* href, const char* src, const char* alt,'''
# The struct continues; find its closing '};' after that
i2 = s.find(anchor2)
if i2 < 0:
    raise SystemExit('open() anchor not found')
close = s.find('};', i2)
if close < 0:
    raise SystemExit('struct close not found')
insert_at = close + 2

serial = '''

// serialize the parsed DOM back to HTML; used to seed the JS DOM shim so
// scripts can find real page elements (getElementById) and the shim tree
// matches the rendered page. Script/Style nodes are skipped (CSS/JS already
// captured by the browser pipeline; their bodies would confuse the seed).
static const char* tag_name_of(Tag t) {
    switch (t) {
        case Tag::H1: return "h1"; case Tag::H2: return "h2"; case Tag::H3: return "h3";
        case Tag::H4: return "h4"; case Tag::H5: return "h5"; case Tag::H6: return "h6";
        case Tag::P: return "p"; case Tag::Div: return "div"; case Tag::Span: return "span";
        case Tag::A: return "a"; case Tag::Img: return "img"; case Tag::Ul: return "ul";
        case Tag::Ol: return "ol"; case Tag::Li: return "li"; case Tag::Br: return "br";
        case Tag::Hr: return "hr"; case Tag::B: return "b"; case Tag::Strong: return "strong";
        case Tag::I: return "i"; case Tag::Em: return "em"; case Tag::Pre: return "pre";
        case Tag::Blockquote: return "blockquote"; case Tag::Table: return "table";
        case Tag::Tr: return "tr"; case Tag::Td: return "td"; case Tag::Th: return "th";
        case Tag::Form: return "form"; case Tag::Button: return "button";
        case Tag::Canvas: return "canvas";
        default: return "";
    }
}
static bool tag_is_void(Tag t) {
    return t == Tag::Br || t == Tag::Hr || t == Tag::Img;
}
static void dom_html_rec(const DomArena& dom, int idx, char* out, int& n, int cap) {
    if (n >= cap - 16) return;
    const DomNode& nd = dom.nodes[idx];
    if (nd.tag == Tag::Text) {
        for (int i = 0; i < nd.text.len() && n < cap - 1; i++) out[n++] = nd.text[i];
        return;
    }
    if (nd.tag == Tag::Root) {
        for (int k = 0; k < nd.kids.size(); k++) dom_html_rec(dom, nd.kids[k], out, n, cap);
        return;
    }
    if (nd.tag == Tag::Script || nd.tag == Tag::Style) return;
    const char* nm = tag_name_of(nd.tag);
    if (!nm[0]) return;
    bool vd = tag_is_void(nd.tag);
    out[n++] = '<';
    for (const char* c = nm; *c && n < cap - 2; c++) out[n++] = *c;
    if (nd.id[0]) {
        const char* a = " id=\\"";
        for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (const char* c = nd.id; *c && n < cap - 2; c++) out[n++] = *c;
        out[n++] = '"';
    }
    if (nd.cls[0]) {
        const char* a = " class=\\"";
        for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (const char* c = nd.cls; *c && n < cap - 2; c++) out[n++] = *c;
        out[n++] = '"';
    }
    if (nd.href.len() > 0) {
        const char* a = " href=\\"";
        for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (int i = 0; i < nd.href.len() && n < cap - 2; i++) out[n++] = nd.href[i];
        out[n++] = '"';
    }
    if (nd.src.len() > 0) {
        const char* a = " src=\\"";
        for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (int i = 0; i < nd.src.len() && n < cap - 2; i++) out[n++] = nd.src[i];
        out[n++] = '"';
    }
    if (vd) { out[n++] = '/'; }
    out[n++] = '>';
    if (!vd) {
        for (int k = 0; k < nd.kids.size(); k++) dom_html_rec(dom, nd.kids[k], out, n, cap);
        if (n < cap - 2) { out[n++] = '<'; out[n++] = '/'; }
        for (const char* c = nm; *c && n < cap - 2; c++) out[n++] = *c;
        if (n < cap - 1) out[n++] = '>';
    }
}
static int dom_to_html(const DomArena& dom, char* out, int cap) {
    int n = 0;
    dom_html_rec(dom, 0, out, n, cap);
    if (n >= cap) n = cap - 1;
    out[n] = 0;
    return n;
}
'''
s = s[:insert_at] + serial + s[insert_at:]
open(p, 'w', encoding='utf-8', newline='').write(s)
print('serializer moved after DomArena')

# ---------- minijs.cpp active site ----------
p3 = r'D:\mycppos1\nefuOS\core\apps\minijs.cpp'
s3 = open(p3, encoding='utf-8').read()
old4 = '''// Declared BEFORE the anonymous namespace: ensure_js() (inside it) uses them,
// and namespace-scope lookup respects declaration order.
static const char* s_env_url = 0;
static const char* s_env_title = 0;
static int s_env_w = 0, s_env_h = 0;

void mini_js_set_env(const char* url, const char* title, int w, int h) {
    s_env_url = url;
    s_env_title = title;
    s_env_w = w; s_env_h = h;
}'''
new4 = '''// Declared BEFORE the anonymous namespace: ensure_js() (inside it) uses them,
// and namespace-scope lookup respects declaration order.
static const char* s_env_url = 0;
static const char* s_env_title = 0;
static const char* s_env_dom = 0;
static int s_env_w = 0, s_env_h = 0;

void mini_js_set_env(const char* url, const char* title, int w, int h) {
    s_env_url = url;
    s_env_title = title;
    s_env_w = w; s_env_h = h;
}
void mini_js_set_dom(const char* html) {
    s_env_dom = html;   // read by ensure_js when the QuickJS runtime is born
}'''
print('minijs active site:', s3.count(old4))
if s3.count(old4) == 1:
    s3 = s3.replace(old4, new4, 1)
    open(p3, 'w', encoding='utf-8', newline='').write(s3)
    print('minijs.cpp ok')
else:
    print('minijs active site FAILED')

# ---------- shim anchor with quotes ----------
p4 = r'D:\mycppos1\nefuOS\core\apps\jsdom_shim.h'
s4 = open(p4, encoding='utf-8').read()
if 'parseSeed(g.__nfuSeed' in s4:
    print('shim already seeded')
else:
    seedjs = '''"  function parseSeed(html){\\n"
"    if (!html) return;\\n"
"    var stack = [bodyNode];\\n"
"    var i = 0, L = html.length;\\n"
"    function pushText(s){ if (!s) return; var p = stack[stack.length-1]; if (!p) return; var tn = new Node('#text'); tn._text = s; p._children.push(tn); tn._parent = p; }\\n"
"    while (i < L){\\n"
"      var lt = html.indexOf('<', i);\\n"
"      if (lt < 0){ pushText(html.substring(i)); break; }\\n"
"      if (lt > i) pushText(html.substring(i, lt));\\n"
"      var gt = html.indexOf('>', lt);\\n"
"      if (gt < 0){ pushText(html.substring(lt)); break; }\\n"
"      var raw = html.substring(lt + 1, gt);\\n"
"      if (raw.charAt(0) === '/'){ if (stack.length > 1) stack.pop(); i = gt + 1; continue; }\\n"
"      var selfClose = raw.charAt(raw.length - 1) === '/';\\n"
"      var m = raw.match(/^[a-zA-Z][a-zA-Z0-9-]*/);\\n"
"      if (!m){ i = gt + 1; continue; }\\n"
"      var tag = m[0].toLowerCase();\\n"
"      var n = new Node(tag);\\n"
"      var am = raw.match(/[a-zA-Z-]+=\\"[^\\"]*\\"/g);\\n"
"      if (am){ var k, eq; for (k=0;k<am.length;k++){ eq = am[k].indexOf('='); n._attrs[am[k].substring(0,eq)] = am[k].substring(eq+2, am[k].length-1); } }\\n"
"      if (n._attrs['id']) byId[n._attrs['id']] = n;\\n"
"      var parent = stack[stack.length-1];\\n"
"      parent._children.push(n); n._parent = parent;\\n"
"      if (!selfClose && tag !== 'br' && tag !== 'hr' && tag !== 'img' && tag !== 'input' && tag !== 'meta' && tag !== 'link') stack.push(n);\\n"
"      i = gt + 1;\\n"
"    }\\n"
"  }\\n"
'''
    old6 = '''"  var nd = new NfuDoc();
"  g.document = nd;'''
    print('shim anchor:', s4.count(old6))
    if s4.count(old6) == 1:
        s4 = s4.replace(old6, seedjs + old6, 1)
        open(p4, 'w', encoding='utf-8', newline='').write(s4)
        print('shim ok')
    else:
        print('shim anchor FAILED')
print('all done')
