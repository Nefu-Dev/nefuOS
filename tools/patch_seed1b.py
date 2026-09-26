# -*- coding: utf-8 -*-
# Redo browser.cpp serializer insertion with correct anchor (is_block_tag)
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()
if 'dom_to_html' in s:
    print('already inserted')
else:
    anchor = '''static bool is_block_tag(Tag t) {'''
    serial = '''// serialize the parsed DOM back to HTML; used to seed the JS DOM shim so
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
        const char* a = " id=\\""; for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (const char* c = nd.id; *c && n < cap - 2; c++) out[n++] = *c;
        out[n++] = '"';
    }
    if (nd.cls[0]) {
        const char* a = " class=\\""; for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (const char* c = nd.cls; *c && n < cap - 2; c++) out[n++] = *c;
        out[n++] = '"';
    }
    if (nd.href.len() > 0) {
        const char* a = " href=\\""; for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
        for (int i = 0; i < nd.href.len() && n < cap - 2; i++) out[n++] = nd.href[i];
        out[n++] = '"';
    }
    if (nd.src.len() > 0) {
        const char* a = " src=\\""; for (const char* c = a; *c && n < cap - 2; c++) out[n++] = *c;
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
    if s.count(anchor) != 1:
        raise SystemExit('anchor is_block_tag not found')
    s = s.replace(anchor, serial + anchor, 1)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('serializer inserted')
