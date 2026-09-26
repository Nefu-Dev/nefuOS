# browser.cpp: external scripts + DOM shim html re-render
p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = open(p, encoding='utf-8').read()

def rep(old, new, tag):
    global s
    c = s.count(old)
    if c != 1:
        print(f'MISS {tag} count={c}')
        return
    s = s.replace(old, new, 1)

# 1) PageParser: script src list support
rep('''    char* title_out;    // <title> text sink (tab title)
    int title_cap;

    PageParser(char* sbuf0 = 0, int* slen0 = 0, CSSRule* cso = 0,
               int* csn = 0, int csmax = 0)
        : html(0), len(0), pos(0), tlen(0), script_buf(sbuf0), script_len(slen0),
          csslen(0), css_out(cso), css_n(csn), css_max(csmax),
          sstream(0), scap(0), slen(0), title_out(0), title_cap(0) {}''',
    '''    char* title_out;    // <title> text sink (tab title)
    int title_cap;
    char (*srcs)[256];   // external <script src> urls (document order)
    int* src_n;
    int src_cap;

    PageParser(char* sbuf0 = 0, int* slen0 = 0, CSSRule* cso = 0,
               int* csn = 0, int csmax = 0,
               char (*srs)[256] = 0, int* srn = 0, int srcap = 0)
        : html(0), len(0), pos(0), tlen(0), script_buf(sbuf0), script_len(slen0),
          csslen(0), css_out(cso), css_n(csn), css_max(csmax),
          sstream(0), scap(0), slen(0), title_out(0), title_cap(0),
          srcs(srs), src_n(srn), src_cap(srcap) {}''', 'pp_ctor')

# 2) <script> handling: src extraction vs inline collection
rep('''            // <script>: collect raw text (run later through mini_js_run)
            if (!closing && t == Tag::Script) {
                while (pos < len && html[pos] != '>') pos++;
                if (pos < len) pos++;''',
    '''            // <script>: inline text is collected; a src=... attribute is
            // remembered (download + run in document order after parsing)
            if (!closing && t == Tag::Script) {
                int tagend = pos;
                while (tagend < len && html[tagend] != '>') tagend++;
                char srcur[256]; int srn2 = 0; int srl = 0;
                {
                    const char* a = html + pos;
                    int al = tagend - pos;
                    for (int ai = 0; ai + 5 <= al; ai++) {
                        if (ci_starts(a + ai, "src=")) {
                            int q = ai + 4;
                            char quote = 0;
                            while (q < al && (a[q] == ' ' || a[q] == '\\t')) q++;
                            if (q < al && (a[q] == '"' || a[q] == '\\'')) { quote = a[q]; q++; }
                            int q2 = q;
                            while (q2 < al && a[q2] != '>' && a[q2] != ' ' && a[q2] != '\\t' &&
                                   (!quote || a[q2] != quote)) q2++;
                            int ul = q2 - q;
                            if (ul > 0) { if (ul > 255) ul = 255; memcpy(srcur, a + q, (size_t)ul); srcur[ul] = 0; srn2 = 1; }
                            break;
                        }
                    }
                }
                pos = tagend;
                if (pos < len) pos++;
                if (srn2) {
                    if (srcs && src_n && src_cap && *src_n < src_cap) {
                        clip_str(srcs[*src_n], 256, srcur);
                        (*src_n)++;
                    }
                    continue;
                }''', 'script_src')

# 3) LoadJob fields
rep('''    char script[SCRIPT_MAX];   // accumulated <script> bytes (NUL at end)
    int script_len;
    char js_out[JS_OUT_MAX];   // mini_js_run console output
    CSSRule css[CSS_MAX];      // parsed <style> sheet
    int css_n;
    void* parser;        // worker-local PageParser (streaming only)
};''',
    '''    char script[SCRIPT_MAX];   // accumulated inline <script> bytes
    int script_len;
    char script_srcs[8][256];  // external <script src> urls (document order)
    int script_src_n;
    char* js_html;       // DOM shim re-sync buffer (worker alloc, main frees)
    int js_html_sz;
    char js_out[4096];   // mini_js_run console output (accumulated)
    CSSRule css[CSS_MAX];      // parsed <style> sheet
    int css_n;
    void* parser;        // worker-local PageParser (streaming only)
};''', 'job_fields')

# 4) load_worker: pass src list, alloc js_html, run external then inline
rep('''    PageParser pp(j->script, &j->script_len, j->css, &j->css_n, CSS_MAX);
    pp.title_out = j->title;
    pp.title_cap = (int)sizeof(j->title);
    j->parser = &pp;
    bool ok = platform_http_stream(j->url, j, on_http_chunk);
    j->parser = 0;
    if (!j->cancelled && (ok || j->aborted)) {
        pp.feed(0, 0, true);   // final flush of any held-back tail
        j->phase = 1;
        DomArena* ar = new DomArena();
        if (ar) {
            ar->nodes = pp.dom.nodes;
            j->dom = ar;
        }
        j->ok = true;
        // execute the accumulated <script> body through the mini JS engine;
        // print/alert output lands in js_out (logged, never on screen)
        if (j->script_len > 0) {
            j->script[j->script_len] = 0;
            nefu::mini_js_set_env(j->url, j->title, 0, 0);
            nefu::mini_js_run(j->script, j->js_out, (int)sizeof(j->js_out));
        }
    }''',
    '''    PageParser pp(j->script, &j->script_len, j->css, &j->css_n, CSS_MAX,
                  j->script_srcs, &j->script_src_n, 8);
    pp.title_out = j->title;
    pp.title_cap = (int)sizeof(j->title);
    j->parser = &pp;
    bool ok = platform_http_stream(j->url, j, on_http_chunk);
    j->parser = 0;
    if (!j->cancelled && (ok || j->aborted)) {
        pp.feed(0, 0, true);   // final flush of any held-back tail
        j->phase = 1;
        DomArena* ar = new DomArena();
        if (ar) {
            ar->nodes = pp.dom.nodes;
            j->dom = ar;
        }
        j->ok = true;
        // execute scripts through the mini JS engine in document order:
        // external <script src> bundles first, then inline <script> bodies.
        // print/alert output lands in js_out (logged, never on screen); the
        // DOM shim writes its serialized tree into js_html for re-render.
        if (j->script_len > 0 || j->script_src_n > 0) {
            j->script[j->script_len] = 0;
            j->js_html_sz = 256 * 1024;
            j->js_html = (char*)kalloc((size_t)j->js_html_sz);
            char jsacc[4096]; int jan = 0;
            for (int si = 0; si < j->script_src_n && si < 8; si++) {
                char u[512];
                resolve_url(j->url, j->script_srcs[si], u, sizeof(u));
                uint8_t* body = 0; uint32_t blen = 0;
                if (platform_http_get(u, &body, &blen) && body && blen > 0) {
                    char* js = (char*)body;
                    js[blen] = 0;
                    nefu::mini_js_set_env(j->url, j->title, 0, 0);
                    nefu::mini_js_run(js, jsacc + jan, (int)sizeof(jsacc) - jan - 1,
                                      j->js_html, j->js_html_sz);
                    jan += (int)strlen(jsacc + jan);
                    if (jan > (int)sizeof(jsacc) - 2) jan = (int)sizeof(jsacc) - 2;
                    kfree(body);
                }
            }
            nefu::mini_js_set_env(j->url, j->title, 0, 0);
            nefu::mini_js_run(j->script, jsacc + jan, (int)sizeof(jsacc) - jan - 1,
                              j->js_html, j->js_html_sz);
            jan += (int)strlen(jsacc + jan);
            if (jan > (int)sizeof(jsacc) - 2) jan = (int)sizeof(jsacc) - 2;
            jsacc[jan] = 0;
            if (jan > 0) { memcpy(j->js_out, jsacc, (size_t)jan); j->js_out[jan] = 0; }
            else j->js_out[0] = 0;
        }
    }''', 'worker_scripts')

# 5) commit_page: after apply_js_commands, re-render the JS-built DOM
rep('''            if (j->js_out[0]) {
                // \\x01 DOM commands mutate the committed DOM; plain console
                // text from print/alert goes to the log, never on screen.
                apply_js_commands(tab, j->js_out);''',
    '''            if (j->js_out[0]) {
                // \\x01 DOM commands mutate the committed DOM; plain console
                // text from print/alert goes to the log, never on screen.
                apply_js_commands(tab, j->js_out);
                if (j->js_html && j->js_html[0]) {
                    // the DOM shim serialized a JS-built tree: re-parse it and
                    // replace the committed DOM (styles stay from the <style>)
                    CSSRule css2[CSS_MAX]; int css2_n = 0;
                    PageParser pp2(0, 0, css2, &css2_n, CSS_MAX);
                    pp2.feed(j->js_html, (int)strlen(j->js_html), true);
                    if (!pp2.dom.overflow) {
                        tab.dom = pp2.dom.nodes;
                        klogf("browser: js dom re-render (%d nodes)", tab.dom.size());
                    } else {
                        klogf("browser: js dom re-render overflow, kept original DOM");
                    }
                }''', 'commit_rerender')

# 6) free js_html in poll_jobs after commit
rep('''        if (tab.used && !j->cancelled && j->ok && j->dom) {''',
    '''        if (j->js_html) { kfree(j->js_html); j->js_html = 0; }
        if (tab.used && !j->cancelled && j->ok && j->dom) {''', 'job_free')

open(p, 'w', encoding='utf-8', newline='').write(s)
print('browser.cpp patch done')
