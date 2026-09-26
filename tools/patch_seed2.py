# -*- coding: utf-8 -*-
# seed call sites in browser.cpp + minijs.h/.cpp + jsdom_shim.h
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()

old = '''                    js[blen] = 0;
                    nefu::mini_js_set_env(j->url, j->title, 0, 0);
                    j->js_err |= (nefu::mini_js_run(js, jsacc + jan, (int)sizeof(jsacc) - jan - 1,'''
new = '''                    js[blen] = 0;
                    nefu::mini_js_set_dom(seedbuf);
                    nefu::mini_js_set_env(j->url, j->title, 0, 0);
                    j->js_err |= (nefu::mini_js_run(js, jsacc + jan, (int)sizeof(jsacc) - jan - 1,'''
print('seed1:', s.count(old))
if s.count(old) == 1:
    s = s.replace(old, new, 1)

old2 = '''            nefu::mini_js_set_env(j->url, j->title, 0, 0);
            j->js_err |= (nefu::mini_js_run(j->script, jsacc + jan, (int)sizeof(jsacc) - jan - 1,'''
new2 = '''            nefu::mini_js_set_dom(seedbuf);
            nefu::mini_js_set_env(j->url, j->title, 0, 0);
            j->js_err |= (nefu::mini_js_run(j->script, jsacc + jan, (int)sizeof(jsacc) - jan - 1,'''
print('seed2:', s.count(old2))
if s.count(old2) == 1:
    s = s.replace(old2, new2, 1)

old3 = '''        if (j->script_len > 0 || j->script_src_n > 0) {
            j->script[j->script_len] = 0;
            j->js_html_sz = 256 * 1024;'''
new3 = '''        if (j->script_len > 0 || j->script_src_n > 0) {
            j->script[j->script_len] = 0;
            // seed the DOM shim with the parsed tree (real getElementById)
            char seedbuf[65536];
            dom_to_html(pp.dom, seedbuf, (int)sizeof(seedbuf));
            j->js_html_sz = 256 * 1024;'''
print('seed3:', s.count(old3))
if s.count(old3) == 1:
    s = s.replace(old3, new3, 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('browser.cpp ok')

p2 = r'D:\mycppos1\nefuOS\core\apps\minijs.h'
s2 = open(p2, encoding='utf-8').read()
oldh = 'void mini_js_set_env(const char* url, const char* title, int w, int h);'
newh = 'void mini_js_set_env(const char* url, const char* title, int w, int h);\nvoid mini_js_set_dom(const char* html);   // seed the DOM shim with the parsed tree'
print('minijs.h:', s2.count(oldh))
if s2.count(oldh) == 1:
    s2 = s2.replace(oldh, newh, 1)
    open(p2, 'w', encoding='utf-8', newline='').write(s2)
    print('minijs.h ok')

p3 = r'D:\mycppos1\nefuOS\core\apps\minijs.cpp'
s3 = open(p3, encoding='utf-8').read()
old4 = '''static const char* s_env_url = 0;
static const char* s_env_title = 0;
static int s_env_w = 0, s_env_h = 0;

void mini_js_set_env(const char* url, const char* title, int w, int h) {
    s_env_url = url;
    s_env_title = title;
    s_env_w = w; s_env_h = h;
}'''
new4 = '''static const char* s_env_url = 0;
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
print('minijs.cpp env:', s3.count(old4))
if s3.count(old4) == 1:
    s3 = s3.replace(old4, new4, 1)
old5 = '''    // React/Vue-capable DOM shim: replaces document with a full node model
    JSValue shr = JS_Eval(s_ctx, JSDOM_SHIM_SRC, (size_t)strlen(JSDOM_SHIM_SRC),'''
new5 = '''    if (s_env_dom && s_env_dom[0])
        JS_SetPropertyStr(s_ctx, g, "__nfuSeed", JS_NewString(s_ctx, s_env_dom));
    // React/Vue-capable DOM shim: replaces document with a full node model
    JSValue shr = JS_Eval(s_ctx, JSDOM_SHIM_SRC, (size_t)strlen(JSDOM_SHIM_SRC),'''
print('minijs.cpp inject:', s3.count(old5))
if s3.count(old5) == 1:
    s3 = s3.replace(old5, new5, 1)
    open(p3, 'w', encoding='utf-8', newline='').write(s3)
    print('minijs.cpp ok')

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
    old6 = '''  var nd = new NfuDoc();
  g.document = nd;'''
    print('shim anchor:', s4.count(old6))
    if s4.count(old6) == 1:
        s4 = s4.replace(old6, seedjs + old6, 1)
        open(p4, 'w', encoding='utf-8', newline='').write(s4)
        print('shim ok')
    else:
        print('shim anchor FAILED')
print('done')
