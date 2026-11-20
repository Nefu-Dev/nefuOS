/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS browser DOM shim (React/Vue-capable node model) — injected into the
// shared QuickJS context before any page script runs. All DOM mutations are
// serialized to an HTML string handed to nativeSync() (C bridge), which the
// browser re-parses and re-layouts. Framework code (React.createElement,
// Vue h(), vanilla DOM) drives the same node API.
#pragma once

static const char* const JSDOM_SHIM_SRC =
" (function(g){\n"
"  function esc(s){ s = String(s); return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;'); }\n"
"  var seq = 0, _all = [];\n"
"  function Node(tag){\n"
"    this._tag = tag; this._parent = null; this._children = [];\n"
"    this._attrs = {}; this._style = {}; this._events = {};\n"
"    this._text = ''; this._inner = null;\n"
"    this._nfuIdx = ++seq; _all[this._nfuIdx] = this;\n"
"  }\n"
"  function styleCss(o){\n"
"    var out = '', k;\n"
"    for (k in o){\n"
"      var v = o[k];\n"
"      if (v === undefined || v === null || v === '') continue;\n"
"      var name = k.replace(/[A-Z]/g, function(m){ return '-' + m.toLowerCase(); });\n"
"      out += name + ':' + v + ';';\n"
"    }\n"
"    return out;\n"
"  }\n"
"  function serialize(n, out){\n"
"    var t = n._tag, i;\n"
"    if (t === '#text'){ out.push(esc(n._text)); return; }\n"
"    if (t === '#comment'){ out.push('<!--' + n._text + '-->'); return; }\n"
"    out.push('<' + t);\n"
"    out.push(' data-nfu=\"' + n._nfuIdx + '\"');\n"
"    for (var k in n._attrs){ var v = n._attrs[k]; if (v !== undefined && v !== null) out.push(' ' + k + '=\"' + esc(v) + '\"'); }\n"
"    // The live value is a property, not an attribute, but the native renderer\n"
"    // paints from this markup - so emit it (after the attribute loop, so it\n"
"    // wins over a stale value=\"\" in the template).\n"
"    if ((t === 'input' || t === 'textarea') && n._val !== undefined && n._val !== null)\n"
"      out.push(' value=\"' + esc(n._val) + '\"');\n"
"    // Control state lives in properties too. The host paints from this markup\n"
"    // and re-parses it into its own DOM, so a ticked box must survive.\n"
"    if (t === 'input' && n._checked) out.push(' checked=\"\"');\n"
"    if (t === 'option' && (n._checked || n._attrs['selected'] !== undefined)) out.push(' selected=\"\"');\n"
"    var st = styleCss(n._style);\n"
"    if (st) out.push(' style=\"' + esc(st) + '\"');\n"
"    out.push('>');\n"
"    if (n._inner !== null) out.push(n._inner);\n"
"    else for (i = 0; i < n._children.length; i++) serialize(n._children[i], out);\n"
"    out.push('</' + t + '>');\n"
"  }\n"
"  function dirty(){\n"
"    // Mark only: the runtime serializes the tree ONCE at the end of the JS\n"
"    // turn (mini_js_run / mini_js_tick). Serializing on every single mutation\n"
"    // made a React-style frame do dozens of full-tree string builds.\n"
"    g.__nfuDirty = 1;\n"
"  }\n"
"  function textOf(n){ var o = '', i; for (i=0;i<n._children.length;i++){ var c = n._children[i]; if (c._tag==='#text') o += c._text; else o += textOf(c); } return o; }\n"
"  // NOTE: walk deliberately does NOT descend into _shadowRoot. Descending was\n"
"  // tried (bd49) to explain a null from findById; the frame body still had its\n"
"  // children and the error offset moved as usual, so shadow trees were not the\n"
"  // cause. Kept out because shadow content leaking into document-wide lookups\n"
"  // changes semantics for every page.\n"
"  function walk(n, arr){ var i; arr.push(n); for (i=0;i<n._children.length;i++) walk(n._children[i], arr); return arr; }\n"
"  function findById(n, id){ var all = walk(n, []), i; for (i=0;i<all.length;i++) if (all[i]._attrs['id'] === id) return all[i];\n"
"    // NEFU-DIAG: the challenge's failing call is `dv('<hex id>')`. Logging the miss here\n"
"    // proves whether dv is this helper (and which tree it searched) or its own element\n"
"    // table, which no shim-side probe can see.\n"
"    if (g.__nfuDumpFrame && typeof id === 'string' && id.charAt(0) === '_' && id.length > 16){\n"
"      g.__nfuFindMissN = (g.__nfuFindMissN || 0) + 1;\n"
"      if (g.__nfuFindMissN < 20){ try { print('[FINDBY-MISS] id=' + id + ' scanned=' + all.length + ' root=' + String(n && n._tag));\n"
"        // NEFU-DIAG: `scanned=1` means the search ran inside an EMPTY shadow root and the\n"
"        // node was never placed there. Show where the sibling subtree actually lives.\n"
"        if (String(n && n._tag) === '#shadow-root'){\n"
"          var _h = n._shadowHost;\n"
"          print('[SHADOW] kids=' + n._children.length + ' innerLen=' + String(n.innerHTML).length\n"
"            + ' host=' + String(_h && _h._tag) + ' hostKids=' + (_h ? _h._children.length : -1));\n"
"          try { var _all2 = walk(g.document.body, []); var _hit = 0;\n"
"            for (var _k = 0; _k < _all2.length; _k++){ var _a3 = _all2[_k]._attrs;\n"
"              if (_a3 && _a3['id'] === id){ _hit = 1; print('[SHADOW-ELSEWHERE] inLightTree under=' + String(_all2[_k]._parent && _all2[_k]._parent._tag)); } }\n"
"            if (!_hit) print('[SHADOW-ELSEWHERE] notInLightTree'); } catch(fm3){}\n"
"          // NEFU-DIAG: the frame may be looking for a node that lives in the PARENT\n"
"          // page's turnstile shadow tree, reached through parent.document. Our walks do\n"
"          // not descend shadow roots, so search the page including shadow trees here.\n"
"          try { var _found = 0;\n"
"            (function ws(x){ if (_found) return; var i2; var k2 = x._children || [];\n"
"              if (x._attrs && x._attrs['id'] === id){ _found = x; return; }\n"
"              for (i2 = 0; i2 < k2.length; i2++) ws(k2[i2]);\n"
"              if (x._shadowRoot) ws(x._shadowRoot); })(g.document.body);\n"
"            print('[SHADOW-CROSS] pageShadowSearch=' + (_found ? ('found under ' + String(_found._parent && _found._parent._tag)) : 'notfound'));\n"
"          } catch(fm4){}\n"
"          // NEFU-DIAG: is the id inside the frame's RAW markup? 'yes' would mean our\n"
"          // parser dropped the node; 'no' means the markup never had it and the builder\n"
"          // that should have produced it never ran.\n"
"          try { var _raw = g.__nfuFrameRaw || '';\n"
"            print('[RAW-IDX] inRawFrame=' + (_raw.indexOf('id=\"' + id + '\"') >= 0 ? 'yes' : 'no') + ' rawLen=' + _raw.length);\n"
"          } catch(fm5){}\n"
"        }\n"
"      } catch(fm1){} }\n"
"    }\n"
"    return null; }\n"
"  function byTag(n, tag, arr){ var i; var t = String(tag).toUpperCase(); if (n.nodeName === t) arr.push(n); for (i=0;i<n._children.length;i++) byTag(n._children[i], t, arr); return arr; }\n"
"  function byClass(n, cls, arr){ var i; var c = n._attrs['class'] || ''; var parts = String(cls).split(/\\s+/); var ok = true, j; for (j=0;j<parts.length;j++) if (parts[j] && c.indexOf(parts[j]) < 0) ok = false; if (ok) arr.push(n); for (i=0;i<n._children.length;i++) byClass(n._children[i], cls, arr); return arr; }\n"
"  function qselAll(n, sel){\n"
"    sel = String(sel).replace(/^\\s+|\\s+$/g, '');\n"
"    var out = [];\n"
"    if (sel === '*') return walk(n, []).slice(1);\n"
"    if (sel.charAt(0) === '#'){ var _h0 = findById(n, sel.slice(1)); if (_h0) return [_h0];\n"
"      // NEFU-FIX: inside a challenge FRAME, qs('#id') has to agree with getElementById,\n"
"      // which auto-creates the mount point: Turnstile resolves nodes through\n"
"      // querySelector('#' + id) and dereferences the result immediately. On the PAGE\n"
"      // side it must stay a strict lookup - app code uses qs('#x') as an existence test\n"
"      // and auto-creating there fabricates nodes and corrupts the whole tree.\n"
"      if (!n._nfuFrame) return [];\n"
"      var _mk = new Node('div'); _mk._attrs['id'] = sel.slice(1);\n"
"      n._children.push(_mk); _mk._parent = n; dirty(); return [_mk]; }\n"
"    if (sel.charAt(0) === '.') return byClass(n, sel.slice(1), []);\n"
"    return byTag(n, sel, []);\n"
"  }\n"
"  function ClassList(n){\n"
"    this._n = n; this._items = [];\n"
"    var c = n._attrs['class'];\n"
"    if (c) this._items = String(c).split(/\\s+/);\n"
"  }\n"
"  ClassList.prototype.add = function(){ var i, a=arguments; for(i=0;i<a.length;i++) if (a[i] && this._items.indexOf(String(a[i]))<0) this._items.push(String(a[i])); this._sync(); };\n"
"  ClassList.prototype.remove = function(){ var i, a=arguments, j; for(i=0;i<a.length;i++){ j = this._items.indexOf(String(a[i])); if (j>=0) this._items.splice(j,1); } this._sync(); };\n"
"  ClassList.prototype.toggle = function(c, f){ var i = this._items.indexOf(String(c)); var want = (f === undefined) ? i < 0 : !!f; if (want && i<0) this._items.push(String(c)); if (!want && i>=0) this._items.splice(i,1); this._sync(); return want; };\n"
"  ClassList.prototype.contains = function(c){ return this._items.indexOf(String(c)) >= 0; };\n"
"  ClassList.prototype._sync = function(){ this._n._attrs['class'] = this._items.join(' '); dirty(); };\n"
"  var P = Node.prototype;\n"
"  // blurhash placeholders call canvas.toDataURL('image/png') right after\n"
"  // createImageData(...).data.set(...).  A missing method was the follow-up\n"
"  // '[MARK][ERR] TypeError: not a function' once the zero-length ImageData\n"
"  // bug was fixed.  Return a valid tiny PNG data URL; the shim never paints\n"
"  // the placeholder anyway.\n"
"  // A canvas that owns a pixel buffer exports a handle instead of an encoded\n"
"  // image: bloxd only feeds toDataURL() back into createImageBitmap(). The\n"
"  // payload must still look like a base64 data URL - callers validate\n"
"  // /^data:image\\/...;base64,/ and atob() it - so the handle is base64 of the\n"
"  // ASCII tag 'NFUCV:<id>' and the decoder resolves that id back to the canvas\n"
"  // which produced it. Canvases with no backing store keep the tiny PNG.\n"
"  P.toDataURL = function(){\n"
"    var px = this._px;\n"
"    if (!px || !this._pxW || !this._pxH) return 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Wl2nXEAAAAASUVORK5CYII=';\n"
"    var reg = g.__cvStore || (g.__cvStore = []);\n"
"    reg.push(this);\n"
"    var tag = 'NFUCV:' + (reg.length - 1), bytes = new Uint8Array(tag.length), i;\n"
"    for (i = 0; i < tag.length; i++) bytes[i] = tag.charCodeAt(i);\n"
"    return 'data:image/x-nfucv;base64,' + _b64enc(bytes);\n"
"  };\n"
"  P.getContext = function(type){\n"
"    var id = this._attrs['id'] || '';\n"
"    if ((type === 'webgl' || type === 'webgl2' || type === 'experimental-webgl') && g.__getGL) return g.__getGL(id);\n"
"    return null;\n"
"  };\n"
"  Object.defineProperty(P, 'nodeType', { get: function(){ return this._tag === '#text' ? 3 : this._tag === '#comment' ? 8 : 1; } , configurable: true});\n"
"  Object.defineProperty(P, 'nodeName', { get: function(){ return this._tag === '#text' ? '#text' : this._tag === '#comment' ? '#comment' : String(this._tag).toUpperCase(); } , configurable: true});\n"
"  Object.defineProperty(P, 'tagName', { get: function(){ return this.nodeName; } , configurable: true});\n"
"  Object.defineProperty(P, 'parentNode', { get: function(){ return this._parent; } , configurable: true});\n"
"  // React 17 reads node.ownerDocument when registering 'selectionchange'\n"
"  // on non-document containers; without it OD(undefined) throws and the\n"
"  // whole app fails to mount.\n"
"  Object.defineProperty(P, 'ownerDocument', { get: function(){ return g.document || null; } , configurable: true});\n"
"  Object.defineProperty(P, 'childNodes', { get: function(){ return this._children; } , configurable: true});\n"
"  // children: element-only collection (real DOM). bloxd.io's deobfuscator\n"
"  // does Array.from(document.body.children) — without this getter the main\n"
"  // bundle throws 'cannot read property Symbol.iterator of undefined' and\n"
"  // the game never boots.\n"
"  Object.defineProperty(P, 'children', { get: function(){\n"
"    var out = [], i;\n"
"    for (i = 0; i < this._children.length; i++){ var c = this._children[i]; if (c._tag !== '#text' && c._tag !== '#comment') out.push(c); }\n"
"    return out;\n"
"  }, configurable: true });\n"
"  Object.defineProperty(P, 'firstChild', { get: function(){ return this._children[0] || null; } , configurable: true});\n"
"  Object.defineProperty(P, 'lastChild', { get: function(){ return this._children[this._children.length-1] || null; } , configurable: true});\n"
"  Object.defineProperty(P, 'nextSibling', { get: function(){ if (!this._parent) return null; var s = this._parent._children, i = s.indexOf(this); return s[i+1] || null; } , configurable: true});\n"
"  Object.defineProperty(P, 'previousSibling', { get: function(){ if (!this._parent) return null; var s = this._parent._children, i = s.indexOf(this); return i > 0 ? s[i-1] : null; } , configurable: true});\n"
"  // Canvas backing-store size is a DOM property, not an attribute. Engines\n"
"  // assign canvas.width/height and never touch the attributes, so without this\n"
"  // the value stayed invisible to the native side: the GL surface was created\n"
"  // at the 300x150 default while the painted box was 600x360. Keep the\n"
"  // attribute in sync too, because the native DOM is rebuilt from serialized\n"
"  // markup (where only attributes exist).\n"
"  Object.defineProperty(P, 'width', {\n"
"    get: function(){ if (this._cw !== undefined) return this._cw; var a = this._attrs['width']; return a === undefined ? 300 : (+a || 0); },\n"
"    set: function(v){ this._cw = (v | 0); this._attrs['width'] = String(this._cw); dirty(); },\n"
"    configurable: true });\n"
"  Object.defineProperty(P, 'height', {\n"
"    get: function(){ if (this._ch !== undefined) return this._ch; var a = this._attrs['height']; return a === undefined ? 150 : (+a || 0); },\n"
"    set: function(v){ this._ch = (v | 0); this._attrs['height'] = String(this._ch); dirty(); },\n"
"    configurable: true });\n"
"  Object.defineProperty(P, 'textContent', { get: function(){ return textOf(this); }, set: function(v){ this._inner = null; this._children.length = 0; this._children.push(g.document.createTextNode(String(v))); dirty(); } , configurable: true});\n"
"  // innerText is the layout-aware sibling of textContent; sites read it to\n"
"  // scrape visible labels, so an alias is enough and its absence silently\n"
"  // yields undefined (and empty diagnostics).\n"
"  Object.defineProperty(P, 'innerText', { get: function(){ return textOf(this); }, set: function(v){ this._inner = null; this._children.length = 0; this._children.push(g.document.createTextNode(String(v))); dirty(); } , configurable: true});\n"
"  Object.defineProperty(P, 'innerHTML', {\n"
"    get: function(){ if (this._inner !== null) return this._inner; var o = [], i; for (i=0;i<this._children.length;i++) serialize(this._children[i], o); return o.join(''); },\n"
"    set: function(v){ this._inner = null; this._children.length = 0; parseHTMLInto(this, String(v));\n"
"      // NEFU-DIAG: distinguishes 'the widget never wrote into its shadow root' from\n"
"      // 'it wrote and our parser produced no nodes' - the two halves the empty-root\n"
"      // finding (bd53/bd54) cannot tell apart on its own.\n"
"      if (g.__nfuDumpFrame && this._tag === '#shadow-root'){\n"
"        g.__nfuShInnerN = (g.__nfuShInnerN || 0) + 1;\n"
"        if (g.__nfuShInnerN < 10) try { print('[SH-INNER] len=' + String(v).length + ' kids=' + this._children.length); } catch(si1){}\n"
"      }\n"
"      // iframes injected via innerHTML bypass appendChild/insertBefore, so\n"
"      // the load hooks never ran (Turnstile mounts its challenge iframe by\n"
"      // writing shadowRoot.innerHTML). Arm any frames the parse produced.\n"
"      try { (function arm(n){ var k = n._children || [], i, c;\n"
"        for (i = 0; i < k.length; i++){ c = k[i];\n"
"          if (c._tag === 'iframe' && c._attrs && c._attrs['src']) nfuLoadFrame(c);\n"
"          else if (c._tag === 'script' && c._attrs && c._attrs['src'] && !c._nfuParsedScript) nfuLoadScript(c);\n"
"          else arm(c); } })(this); } catch(e2){} },\n"
"    configurable: true\n"
"  });\n"
"  Object.defineProperty(P, 'className', { get: function(){ return this._attrs['class'] || ''; }, set: function(v){ this._attrs['class'] = String(v); dirty(); } , configurable: true});\n"
"  Object.defineProperty(P, 'id', { get: function(){ return this._attrs['id'] || ''; }, set: function(v){ this._attrs['id'] = String(v); if (g.__nfuDumpFrame && g.__nfuIdLog !== 0){ g.__nfuIdLog = (g.__nfuIdLog || 0) + 1; if (g.__nfuIdLog < 40) try { print('[ID-SET] ' + v + (this._nfuFrameEl ? ' (frame el)' : '')); } catch(pi){} } dirty(); } , configurable: true});\n"
"  function styleApi(st){\n"
"    if (st.__cssApi) return st;\n"
"    Object.defineProperty(st, '__cssApi', { value: true, enumerable: false , configurable: true});\n"
"    // non-enumerable so styleCss()/clone for..in loops stay clean\n"
"    Object.defineProperty(st, 'setProperty', { value: function(k, v){ this[k] = (v === null || v === undefined) ? '' : String(v); }, writable: true, configurable: true });\n"
"    Object.defineProperty(st, 'getPropertyValue', { value: function(k){ var v = this[k]; return (v === undefined || typeof v === 'function') ? '' : String(v); }, writable: true, configurable: true });\n"
"    Object.defineProperty(st, 'removeProperty', { value: function(k){ var v = this[k]; delete this[k]; return (v === undefined) ? '' : String(v); }, writable: true, configurable: true });\n"
"    Object.defineProperty(st, 'getPropertyPriority', { value: function(){ return ''; }, writable: true, configurable: true });\n"
"    Object.defineProperty(st, 'item', { value: function(){ return ''; }, writable: true, configurable: true });\n"
"    Object.defineProperty(st, 'length', { value: 0, writable: true, configurable: true });\n"
"    return st;\n"
"  }\n"
"  Object.defineProperty(P, 'style', { get: function(){ return styleApi(this._style); }, set: function(v){ this._style = v; dirty(); } , configurable: true});\n"
"  Object.defineProperty(P, 'classList', { get: function(){ return new ClassList(this); } , configurable: true});\n"
"  Object.defineProperty(P, 'style', { get: function(){ return styleApi(this._style); }, set: function(v){ this._style = v; dirty(); } , configurable: true});\n"
"  // ---- dynamic <script src> loading -------------------------------------\n"
"  // Real sites inject third-party code at runtime by appending <script>\n"
"  // elements and awaiting onload (bloxd.io's Turnstile gate held the whole\n"
"  // game boot until its loader fired; AMD loaders do the same). A silent\n"
"  // no-op left those awaits pending forever. Fetch + global eval + fire\n"
"  // load/error like a real browser. Known third-party glue we cannot use\n"
"  // (captcha libs) is answered with a tiny local stub instead of a network\n"
"  // fetch, so gated boot paths resolve instead of stalling.\n"
"  function nfuResolveUrl(u){\n"
"    u = String(u);\n"
"    if (/^https?:\\/\\//.test(u) || u.indexOf('data:') === 0) return u;\n"
"    var base = (g.location && g.location.href) || '';\n"
"    if (!base) return u;\n"
"    if (u.charAt(0) === '/') return base.replace(/^(https?:\\/\\/[^\\/]+).*/, '$1') + u;\n"
"    return base.replace(/[^\\/]*$/, '') + u;\n"
"  }\n"
"  function nfuFireScriptEvent(n, type){\n"
"    var ev = { type: type, target: n, currentTarget: n, bubbles: false, cancelable: false, defaultPrevented: false, preventDefault: function(){ this.defaultPrevented = true; }, stopPropagation: function(){} };\n"
"    var h = n['on' + type];\n"
"    if (h) try { h.call(n, ev); } catch(e){ try { print('[SCRIPT-HANDLER ' + type + '] ' + e); } catch(pe){} }\n"
"    var a = n._events[type];\n"
"    if (a) for (var i = 0; i < a.length; i++) try { a[i].call(n, ev); } catch(e2){ try { print('[SCRIPT-LISTENER ' + type + '] ' + e2); } catch(pe){} }\n"
"  }\n"
"  function nfuStubFor(url){\n"
"    // NOTE: the old Turnstile stub is GONE — challenges.cloudflare.com is\n"
"    // reachable from this environment, so the REAL api.js is loaded and the\n"
"    // real widget produces a server-verifiable token for /index/traffic-code.\n"
"    if (/recaptcha\\.(net|com)\\/recaptcha\\/api\\.js/.test(url)) {\n"
"      return \"window.grecaptcha={ready:function(cb){try{cb()}catch(e){}},execute:function(){return Promise.resolve('nfu-recaptcha-ok')},render:function(){return 1},reset:function(){},getResponse:function(){return 'nfu-recaptcha-ok'}};\";\n"
"    }\n"
"    // AdinPlay ad SDK: bloxd's ad-SDK selector awaits this script before it\n"
"    // sets initSdkSelectorFinished — a failed load kept the game boot gated\n"
"    // at \"Loading required libraries\" forever. The adaptor degrades\n"
"    // gracefully when the ad player is missing, so an inert tag is enough.\n"
"    if (/api\\.adinplay\\.com\\/libs\\/aiptag/.test(url)) {\n"
"      return \"window.aiptag=window.aiptag||{cmd:{player:[],display:[]}};window.aipDisplayTag=window.aipDisplayTag||{};window.__VM=window.__VM||function(f){try{f(null,{Instances:{pageManager:{newPageSession:function(){}}}})}catch(e){}};\";\n"
"    }\n"
"    return null;\n"
"  }\n"
"  function nfuOnloadParam(url){\n"
"    var m = String(url).match(/[?&]onload=([\\w$]+)/);\n"
"    return m ? m[1] : null;\n"
"  }\n"
"  // Static-src denylist mirrors the host's kSkipSrcs: analytics/ad beacons\n"
"  // are answered as successfully loaded (no eval) so their callbacks don't\n"
"  // wedge boot paths either.\n"
"  var kSkipDyn = ['recaptcha', 'page-kit.io', 'cloudflareinsights', 'vconsole', 'google-analytics.com', 'googletagmanager.com', 'googlesyndication.com', 'doubleclick.net', 'adsbygoogle', 'hotjar.com', 'sentry-cdn', 'connect.facebook.net', 'static.cloudflareinsights.com', 'mc.yandex.ru', 'clarity.ms'];\n"
"  function nfuLoadScript(n){\n"
"    if (n._nfuLoaded || n._nfuLoading || n._nfuNoExec) return;\n"
"    if (n._tag !== 'script') return;\n"
"    var src = n._attrs['src'] || '';\n"
"    if (!src) return;\n"
"    try { print('[DYN-REQ] ' + String(src).slice(0, 110)); } catch(pe){}\n"
"    n._nfuLoading = 1;\n"
"    var url = nfuResolveUrl(src);\n"
"    (g.__nfuTimers = g.__nfuTimers || []).push(function(){\n"
"      n._nfuLoading = 0; n._nfuLoaded = 1;\n"
"      var stub = nfuStubFor(url);\n"
"      if (stub !== null){\n"
"        var err0 = '';\n"
"        try { err0 = String(__nfuEvalGlobal(stub, url)); } catch(e0){ err0 = String(e0); }\n"
"        if (err0){ try { print('[DYN-SCRIPT-ERR] ' + url + ' :: ' + err0); } catch(pe){} nfuFireScriptEvent(n, 'error'); return; }\n"
"        var ol0 = nfuOnloadParam(url);\n"
"        if (ol0 && g[ol0]) try { g[ol0](); } catch(e1){ try { print('[DYN-ONLOAD] ' + e1); } catch(pe){} }\n"
"        try { print('[DYN-SCRIPT] stub ' + url); } catch(pe){}\n"
"        nfuFireScriptEvent(n, 'load');\n"
"        return;\n"
"      }\n"
"      for (var ki = 0; ki < kSkipDyn.length; ki++){\n"
"        if (url.indexOf(kSkipDyn[ki]) >= 0){\n"
"          var ol2 = nfuOnloadParam(url);\n"
"          if (ol2 && g[ol2]) try { g[ol2](); } catch(e2){}\n"
"          nfuFireScriptEvent(n, 'load');\n"
"          return;\n"
"        }\n"
"      }\n"
"      var code = '';\n"
"      try { code = String(__nfuHttpGet(url)); } catch(e3){\n"
"        try { print('[DYN-SCRIPT-FAIL] ' + url + ' :: ' + e3); } catch(pe){}\n"
"        nfuFireScriptEvent(n, 'error');\n"
"        return;\n"
"      }\n"
"      var err = '';\n"
"      try { err = String(__nfuEvalGlobal(code, url)); } catch(e4){ err = String(e4); }\n"
"      if (err){\n"
"        try { print('[DYN-SCRIPT-ERR] ' + url + ' :: ' + err); } catch(pe){}\n"
"        nfuFireScriptEvent(n, 'error');\n"
"        return;\n"
"      }\n"
"      try { print('[DYN-SCRIPT] ' + url + ' ' + code.length); } catch(pe){}\n"
"      nfuFireScriptEvent(n, 'load');\n"
"    });\n"
"  }\n"
"  // <link rel=stylesheet> dynamic loading: webpack's CSS chunk loader\n"
"  // (Qi.f.miniCss) appends a <link> and awaits its onload before resolving\n"
"  // Qi.e(chunkId) — bloxd's engine chunk 9 carries CSS, so without this the\n"
"  // whole game boot gate hung at \"Loading required libraries 0%\" forever.\n"
"  // The fetched CSS is appended to <head> as a real <style> node so the\n"
"  // native renderer's CSS parser picks it up on the next re-render.\n"
"  function nfuLoadLink(n){\n"
"    if (n._nfuLoaded || n._nfuLoading) return;\n"
"    var rel = String(n._attrs['rel'] || '').toLowerCase();\n"
"    var href = n._attrs['href'] || '';\n"
"    if (rel.indexOf('stylesheet') < 0 || !href){\n"
"      // preload/icon/modulepreload etc.: real browsers resolve these off the\n"
"      // critical path; nothing awaits their onload for correctness\n"
"      n._nfuLoaded = 1;\n"
"      (g.__nfuTimers = g.__nfuTimers || []).push(function(){ nfuFireScriptEvent(n, 'load'); });\n"
"      return;\n"
"    }\n"
"    n._nfuLoading = 1;\n"
"    var url = nfuResolveUrl(href);\n"
"    (g.__nfuTimers = g.__nfuTimers || []).push(function(){\n"
"      n._nfuLoading = 0; n._nfuLoaded = 1;\n"
"      var css = '';\n"
"      try { css = String(__nfuHttpGet(url)); } catch(e){\n"
"        try { print('[DYN-CSS-FAIL] ' + url); } catch(pe){}\n"
"        nfuFireScriptEvent(n, 'error');\n"
"        return;\n"
"      }\n"
"      if (css && typeof headNode !== 'undefined' && headNode){\n"
"        var st = new Node('style');\n"
"        st._inner = css;\n"
"        st._parent = headNode;\n"
"        headNode._children.push(st);\n"
"        dirty();\n"
"      }\n"
"      try { print('[DYN-CSS] ' + url + ' ' + css.length); } catch(pe){}\n"
"      nfuFireScriptEvent(n, 'load');\n"
"    });\n"
"  }\n"
"  // ---- iframe browsing context (Cloudflare Turnstile needs this) ----\n"
"  // The Turnstile parent api.js renders a challenge iframe pointing at\n"
"  // challenges.cloudflare.com/cdn-cgi/challenge-platform/... and talks to it\n"
"  // via postMessage. We fetch the frame HTML, parse it into a detached body,\n"
"  // and eval its scripts inside a wrapper function whose window/document/\n"
"  // parent/location params shadow the page globals.\n"
"  function gWinDispatch(d, org, srcWin){\n"
"    try { print('[PARENT-MSG] ' + JSON.stringify(d).slice(0, 140)); } catch(pm){}\n"
"    (g.__nfuTimers = g.__nfuTimers || []).push(function(){\n"
"      var ls = (g.__nfuWinEvents['message'] = g.__nfuWinEvents['message'] || []).slice();\n"
"      var ev = { data: d, origin: String(org || ''), source: srcWin || null, type: 'message' };\n"
"      for (var i = 0; i < ls.length; i++){ try { ls[i].call(g, ev); } catch(e){ try { print('[MSG-ERR] ' + (e && e.message ? e.message : e)); } catch(pe){} } }\n"
"    });\n"
"  }\n"
"  function nfuLoadFrame(n){\n"
"    g.__nfuNfCalls = (g.__nfuNfCalls|0) + 1;\n"
"    try { print('[NF-CALL] tag=' + n._tag + ' src=' + String(n._attrs && n._attrs['src']).slice(0, 80) + ' loading=' + (n._nfuFrameLoading|0) + ' loaded=' + (n._nfuFrameLoaded|0)); } catch(pz0){}\n"
"    if (n._nfuFrameLoading || n._nfuFrameLoaded) return;\n"
"    if (n._tag !== 'iframe') return;\n"
"    var href = n._attrs['src'] || '';\n"
"    if (!href || href.charAt(0) === '#') return;\n"
"    n._nfuFrameLoading = 1;\n"
"    var url = nfuResolveUrl(href);\n"
"    if (g.__nfuTsDebug && url.indexOf('challenge-platform') >= 0 && url.indexOf('__cfDebugTurnstileOutcome') < 0){ url += (url.indexOf('?') >= 0 ? '&' : '?') + '__cfDebugTurnstileOutcome=' + encodeURIComponent(String(g.__nfuTsDebug)); }\n"
"    var mO = String(url).match(/^([a-zA-Z]+:)\\/\\/([^\\/?#]+)/);\n"
"    var fOrigin = mO ? (mO[1] + '//' + mO[2]) : '';\n"
"    var gOrigin = g.location ? (g.location.origin || '') : '';\n"
"    (g.__nfuTimers = g.__nfuTimers || []).push(function(){\n"
"      try { __nfuExtendDeadline(150); } catch(exd){}\n"
"      var html = '';\n"
"      try { html = String(__nfuHttpGet(url)); } catch(e){\n"
"        try { print('[DYN-IFRAME-FAIL] ' + url); } catch(pe){}\n"
"        n._nfuFrameLoading = 0;\n"
"        nfuFireScriptEvent(n, 'error');\n"
"        return;\n"
"      }\n"
"      try { print('[DYN-IFRAME] ' + url + ' ' + html.length); } catch(pe0){}\n"
"      // NEFU-DIAG: keep the raw frame markup so a later id-lookup miss can be classified\n"
"      // as 'the parser dropped it' vs 'the markup never contained it'.\n"
"      if (g.__nfuDumpFrame){ g.__nfuFrameRaw = html; }\n"
"      var fbody = new Node('body'); fbody._nfuFrame = 1;   // marks the auto-create scope\n"
"      try { parseHTMLInto(fbody, html); } catch(pe1){}\n"
"      var fEvents = {};\n"
"      var frameWin = {};\n"
"      var frameDoc = {\n"
"        nodeType: 9, readyState: 'loading', cookie: '', title: '', referrer: '',\n"
"        URL: url, documentURI: url, baseURI: url, domain: (mO ? mO[2] : ''),\n"
"        body: fbody, documentElement: fbody, head: fbody, activeElement: fbody,\n"
"        defaultView: frameWin, currentScript: null, hidden: false, visibilityState: 'visible',\n"
"        createElement: function(t){ var el = new Node(String(t).toLowerCase()); el._nfuFrameEl = 1; if (el._tag === 'script') el._nfuNoExec = 1; try { var rg = g.__nfuTagRing || (g.__nfuTagRing = []); rg.push(el._tag); if (rg.length > 90) rg.shift(); } catch(_tge){} if (g.__nfuDumpFrame && g.__nfuElN !== 0){ g.__nfuElN = (g.__nfuElN || 0) + 1; if (g.__nfuElN < 60) try { print('[NEW-EL] ' + el._tag); } catch(pne){} } return el; },\n"
"        createElementNS: function(ns, t){ var el2 = new Node(String(t || 'div').toLowerCase()); el2._nfuFrameEl = 1; if (el2._tag === 'script') el2._nfuNoExec = 1; return el2; },\n"
"        createTextNode: function(s){ var t2 = new Node('#text'); t2._text = String(s); return t2; },\n"
"        createComment: function(s){ var t3 = new Node('#comment'); t3._text = String(s); return t3; },\n"
"        createDocumentFragment: function(){ return new Node('#fragment'); },\n"
"        createEvent: function(){ return { initEvent: function(t4){ this.type = t4; } }; },\n"
"        createRange: function(){ return { setStart: function(){}, setEnd: function(){}, collapse: function(){}, selectNodeContents: function(){}, getBoundingClientRect: function(){ return { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 }; }, getClientRects: function(){ return []; } }; },\n"
"        attachShadow: function(opts){ var s4 = new Node('#shadow-root'); s4._shadowMode = (opts && opts.mode) || 'open'; return s4; },\n"
"        getElementById: function(id){ id = String(id); var r0 = findById(fbody, id); if (r0) return r0;\n"
"          if (g.__nfuDumpFrame){ try { print('[FRAME-ID-MISS] ' + id); } catch(pm){} }\n"
"          // NEFU-FIX: the challenge builder calls dv('<hex id>') for a node it registered\n"
"          // a moment earlier and dereferences the result without a guard. Chromium always\n"
"          // hands back that node; here the mount point never reached the frame tree, so the\n"
"          // lookup returned null and the whole frame script died on it. Auto-create exactly\n"
"          // like the page document does, so the real script keeps running instead of\n"
"          // throwing on a null that a real browser would never produce.\n"
"          var nn = new Node('div'); nn._attrs['id'] = id; nn._nfuFrameEl = 1;\n"
"          fbody._children.push(nn); nn._parent = fbody; dirty();\n"
"          return nn; },\n"
"        getElementsByTagName: function(t5){ return byTag(fbody, t5, []); },\n"
"        getElementsByClassName: function(c2){ return byClass(fbody, c2, []); },\n"
"        querySelector: function(s2){ var a = qselAll(fbody, s2); if (!a.length && g.__nfuDumpFrame){ try { print('[FRAME-QS-MISS] ' + s2); } catch(pq){} } return a.length ? a[0] : null; },\n"
"        querySelectorAll: function(s3){ return qselAll(fbody, s3); },\n"
"        addEventListener: function(t6, f2){ (fEvents[t6] = fEvents[t6] || []).push(f2); },\n"
"        removeEventListener: function(t7, f3){ var a2 = fEvents[t7]; if (a2){ var i2 = a2.indexOf(f3); if (i2 >= 0) a2.splice(i2, 1); } },\n"
"        dispatchEvent: function(ev){ var a3 = fEvents[ev.type] || []; for (var i3 = 0; i3 < a3.length; i3++){ try { a3[i3].call(frameWin, ev); } catch(e4){} } return true; },\n"
"        write: function(){}, writeln: function(){}, open: function(){ return null; }, close: function(){},\n"
"        hasFocus: function(){ return true; }\n"
"      };\n"
"      frameWin.window = frameWin; frameWin.self = frameWin; frameWin.frames = [frameWin];\n"
"      frameWin.document = frameDoc;\n"
"      var docReads = 0;\n"
"      frameDoc = new Proxy(frameDoc, { get: function(t, k){ if (typeof k === 'string' && docReads < 60 && k !== 'nodeType'){ docReads++; try { print('[DOC-READ] ' + k + (t[k] === undefined ? ' =undefined' : '')); } catch(pd){} } return t[k]; } });\n"
"      frameWin.location = { href: url, toString: function(){ return url; }, origin: fOrigin, protocol: (mO ? mO[1] : 'https:'), host: (mO ? mO[2] : ''), hostname: (mO ? mO[2] : ''), pathname: '/', search: '', hash: '', reload: function(){}, replace: function(){} };\n"
"      frameWin.origin = fOrigin;\n"
"      frameWin.navigator = g.navigator;\n"
"      frameWin.crypto = g.crypto;\n"
"      frameWin.performance = g.performance;\n"
"      frameWin.setTimeout = g.setTimeout; frameWin.clearTimeout = g.clearTimeout;\n"
"      frameWin.setInterval = g.setInterval; frameWin.clearInterval = g.clearInterval;\n"
"      frameWin.requestAnimationFrame = g.requestAnimationFrame || function(f4){ return g.setTimeout(f4, 16); };\n"
"      frameWin.addEventListener = frameDoc.addEventListener;\n"
"      frameWin.removeEventListener = frameDoc.removeEventListener;\n"
"      frameWin.dispatchEvent = frameDoc.dispatchEvent;\n"
"      // parent-facing surface: postMessage targets the page's 'message'\n"
"      // listeners with source = this frame; everything else minimal.\n"
"      var frameParent = {\n"
"        postMessage: function(d, org){ gWinDispatch(d, org, frameWin); },\n"
"        addEventListener: function(t8, f5){ g.addEventListener(t8, f5); },\n"
"        removeEventListener: function(t9, f6){ g.removeEventListener(t9, f6); },\n"
"        location: { href: gOrigin + '/', origin: gOrigin, replace: function(){}, reload: function(){} },\n"
"        frames: [frameWin]\n"
"      };\n"
"      frameWin.parent = frameParent; frameWin.top = frameParent; frameWin.opener = null;\n"
"      // messages FROM the page INTO this frame. Single-global model: the\n"
"      // frame's window IS the page global, so its handlers registered via\n"
"      // window.addEventListener live in g.__nfuWinEvents['message'] —\n"
"      // deliver there (both directions share the one list).\n"
"      frameWin.postMessage = function(d, org){\n"
"        try { print('[FRAME-IN] ' + JSON.stringify(d).slice(0, 140)); } catch(pm2){}\n"
"        gWinDispatch(d, String(org || gOrigin), g);\n"
"      };\n"
"      frameWin.close = function(){}; frameWin.focus = function(){}; frameWin.blur = function(){};\n"
"      frameWin.innerWidth = g.innerWidth; frameWin.innerHeight = g.innerHeight;\n"
"      frameWin.devicePixelRatio = g.devicePixelRatio || 1;\n"
"      frameWin.getComputedStyle = g.getComputedStyle;\n"
"      frameWin.history = { length: 1, state: null, back: function(){}, forward: function(){}, pushState: function(){}, replaceState: function(){} };\n"
"      frameWin.localStorage = g.localStorage; frameWin.sessionStorage = g.sessionStorage;\n"
"      frameWin.Image = g.Image; frameWin.XMLHttpRequest = g.XMLHttpRequest; frameWin.fetch = g.fetch;\n"
"      frameWin.URL = g.URL; frameWin.URLSearchParams = g.URLSearchParams;\n"
"      frameWin.TextEncoder = g.TextEncoder; frameWin.TextDecoder = g.TextDecoder;\n"
"      // The challenge platform runs its bootstrap inside this frame's window,\n"
"      // so every global we added for it must be mirrored here as well.\n"
"      frameWin.Intl = g.Intl; frameWin.crypto = g.crypto;\n"
"      frameWin.createImageBitmap = g.createImageBitmap; frameWin.Audio = g.Audio;\n"
"      frameWin.CSS = g.CSS; frameWin.getSelection = g.getSelection;\n"
"      frameWin.Blob = g.Blob; frameWin.FormData = g.FormData; frameWin.AbortController = g.AbortController;\n"
"      frameWin.WebSocket = g.WebSocket; frameWin.Worker = g.Worker; frameWin.Request = g.Request;\n"
"      frameWin.Response = g.Response; frameWin.Headers = g.Headers; frameWin.Event = g.Event;\n"
"      frameWin.CustomEvent = g.CustomEvent; frameWin.MessageChannel = g.MessageChannel;\n"
"      // NOTE: do NOT point `globalThis` at frameWin or set frameWin.__proto__ = g.\n"
"      // Both were tried in bd39-bd41 to give the frame a真实 iframe-like global.\n"
"      // They did not stop the challenge's null derefs and they did regress the\n"
"      // main page (bloxd social refresh hit Err(interrupted) again), so the frame\n"
"      // keeps the single-global model for now.\n"
"      frameWin.MessagePort = g.MessagePort; frameWin.atob = g.atob; frameWin.btoa = g.btoa;\n"
"      frameWin.parseInt = g.parseInt; frameWin.parseFloat = g.parseFloat;\n"
"      frameWin.isFinite = g.isFinite; frameWin.isNaN = g.isNaN;\n"
"      frameWin.Object = g.Object; frameWin.Array = g.Array; frameWin.JSON = g.JSON;\n"
"      frameWin.Math = g.Math; frameWin.Date = g.Date; frameWin.Promise = g.Promise;\n"
"      frameWin.String = g.String; frameWin.Number = g.Number; frameWin.Boolean = g.Boolean;\n"
"      frameWin.RegExp = g.RegExp; frameWin.Error = g.Error; frameWin.TypeError = g.TypeError;\n"
"      frameWin.RangeError = g.RangeError; frameWin.SyntaxError = g.SyntaxError;\n"
"      frameWin.Uint8Array = g.Uint8Array; frameWin.Int8Array = g.Int8Array;\n"
"      frameWin.Uint8ClampedArray = g.Uint8ClampedArray; frameWin.Int16Array = g.Int16Array;\n"
"      frameWin.Uint16Array = g.Uint16Array; frameWin.Int32Array = g.Int32Array;\n"
"      frameWin.Uint32Array = g.Uint32Array; frameWin.Float32Array = g.Float32Array;\n"
"      frameWin.Float64Array = g.Float64Array; frameWin.ArrayBuffer = g.ArrayBuffer;\n"
"      frameWin.DataView = g.DataView; frameWin.Map = g.Map; frameWin.Set = g.Set;\n"
"      frameWin.WeakMap = g.WeakMap; frameWin.WeakSet = g.WeakSet; frameWin.Symbol = g.Symbol;\n"
"      frameWin.Reflect = g.Reflect; frameWin.BigInt = g.BigInt; frameWin.DataView = g.DataView;\n"
"      frameWin.encodeURIComponent = g.encodeURIComponent; frameWin.decodeURIComponent = g.decodeURIComponent;\n"
"      frameWin.encodeURI = g.encodeURI; frameWin.decodeURI = g.decodeURI;\n"
"      frameWin.escape = g.escape; frameWin.unescape = g.unescape;\n"
"      frameWin.screen = g.screen; frameWin.deviceMemory = g.deviceMemory;\n"
"      frameWin.globalThis = frameWin;\n"
"      n.contentWindow = frameWin; n.contentDocument = frameDoc;\n"
"      var frameLoc = frameWin.location;\n"
"      var framePostToSelf = frameWin.postMessage;\n"
"      var scrs = byTag(fbody, 'script', []);\n"
"      try { print('[FRAME-SCRIPTS] ' + scrs.length + ' scripts, bodykids=' + fbody._children.length); } catch(pz9){}\n"
"      for (var si = 0; si < scrs.length; si++){\n"
"        var sc = scrs[si];\n"
"        var code = sc._inner || textOf(sc);\n"
"        var ssrc = sc._attrs['src'] || '';\n"
"        try {\n"
"          if (ssrc && !code){ code = String(__nfuHttpGet(nfuResolveUrl(ssrc))); }\n"
"          if (!code) continue;\n"
"          try { if (g.__nfuDumpFrame && !g.__nfuFrameDumped && String(code).length > 200000){ g.__nfuFrameDumped = 1; var _cd = String(code), _ci; for (_ci = 0; _ci < _cd.length; _ci += 200){ print('[FD]' + _cd.slice(_ci, _ci + 200)); } print('[FD-END] len=' + _cd.length); } } catch(pfd){}\n"
"          frameDoc.currentScript = sc;\n"
"          // Single-global frame model: the challenge scripts mix\n"
"          // `window.x = ...` (identifier via proxy) with sloppy-mode `this`\n"
"          // reads (the REAL global object). For both to see the same object,\n"
"          // frame code must run against g itself — only frame-specific\n"
"          // bindings (document/location/parent/events) are overridden through\n"
"          // the with-scope. Missed identifiers are logged for diagnosis.\n"
"          var missLog = {};\n"
"          // Object-member misses: the challenge code reads a method off a real\n"
"          // host object (navigator/screen/...), gets undefined because we do not\n"
"          // implement it, then calls it. That surfaces only as the anonymous\n"
"          // \"not a function (got undefined)\" with no name, so wrap the host\n"
"          // objects the frame touches and log the member that came back empty.\n"
"          var _wrapCache = {}, _wrapMiss = {};\n"
"          function _nfuMissWrap(name, obj){\n"
"            if (!obj || typeof obj !== 'object') return obj;\n"
"            try {\n"
"              return new Proxy(obj, { get: function(t, k){\n"
"                if (typeof k !== 'string') return t[k];\n"
"                var v = t[k];\n"
"                if (v === undefined){ var mk = name + '.' + k;\n"
"                  if (!_wrapMiss[mk]){ _wrapMiss[mk] = 1; try { print('[FRAME-MISS-OBJ] ' + mk); } catch(pmo){} } }\n"
"                return v;\n"
"              } });\n"
"            } catch(ew){ return obj; }\n"
"          }\n"
"          var _wrapNames = { navigator: 1, screen: 1, performance: 1, crypto: 1, localStorage: 1, sessionStorage: 1, history: 1, devicePixelRatio: 0 };\n"
"          var scopeProxy = new Proxy({}, { get: function(t, k){\n"
"            if (typeof k !== 'string') return g[k];\n"
"            if (k === 'document') return frameDoc;\n"
"            if (k === 'location') return frameLoc;\n"
"            if (k === 'parent' || k === 'top') return frameParent;\n"
"            if (k === 'postMessage') return framePostToSelf;\n"
"            if (k === 'addEventListener') return frameDoc.addEventListener;\n"
"            if (k === 'removeEventListener') return frameDoc.removeEventListener;\n"
"            if (k === 'dispatchEvent') return frameDoc.dispatchEvent;\n"
"            if (k === 'origin') return fOrigin;\n"
"            if (k === 'setTimeout') return function(f7, d2){\n"
"              try { print('[FRAME-SETTMO] ' + String(d2) + 'ms src=' + String(f7).slice(0, 220)); } catch(pz1){}\n"
"              return g.setTimeout(function(){\n"
"                try { __nfuExtendDeadline(60); } catch(exd){}\n"
"                try { print('[FRAME-TMO-RUN] ' + String(d2) + 'ms'); } catch(pz3){}\n"
"                try { f7(); } catch(eT){ try {\n"
"                  print('[FRAME-TMO-ERR] msg=' + String(eT && eT.message) + ' || ' + String(eT && eT.stack).slice(0, 700));\n"
"                  // NEFU-DIAG: the throw is `findById(<body>, '_<24 hex>')` followed by a\n"
"                  // property read, and findById walks the tree - so the only way it returns\n"
"                  // null is an empty body. Snapshot the body at the throw to confirm.\n"
"                  var _fb = frameDoc.body;\n"
"                  print('[FRAME-BODY] kids=' + (_fb ? _fb._children.length : -1) + ' html=' + String(_fb && _fb.innerHTML).slice(0, 240));\n"
"                  // NEFU-DIAG: count nodes whose id carries the challenge's '_<hex>' shape,\n"
"                  // separately for the frame document and the page document. This decides\n"
"                  // between 'the widget tree was never built' and 'it was built into\n"
"                  // another document than the one the lookup walks'.\n"
"                  var _cA = 0, _cB = 0, _sA = '';\n"
"                  var _wa = walk(_fb, []);\n"
"                  for (var _i = 0; _i < _wa.length; _i++){ var _idd = _wa[_i]._attrs && _wa[_i]._attrs['id'];\n"
"                    if (_idd && String(_idd).charAt(0) === '_'){ _cA++; if (_sA.length < 120) _sA += _idd + ' '; } }\n"
"                  try { var _wd = walk(g.document.body, []);\n"
"                    for (var _j = 0; _j < _wd.length; _j++){ var _id2 = _wd[_j]._attrs && _wd[_j]._attrs['id'];\n"
"                      if (_id2 && String(_id2).charAt(0) === '_') _cB++; } } catch(fb2){}\n"
"                  print('[FRAME-IDS] frameUnderscore=' + _cA + ' pageUnderscore=' + _cB + ' sample=' + _sA);\n"
"                } catch(peT){} }\n"
"              }, d2);\n"
"            };\n"
"            if (k === 'setInterval') return function(f8, d3){\n"
"              try { print('[FRAME-SETIV] ' + String(d3) + 'ms src=' + String(f8).slice(0, 160)); } catch(pz4){}\n"
"              return g.setInterval(function(){\n"
"                try { __nfuExtendDeadline(60); } catch(exd2){}\n"
"                try { f8(); } catch(eI){ try { print('[FRAME-IV-ERR] msg=' + String(eI && eI.message) + ' || ' + String(eI && eI.stack).slice(0, 700)); } catch(peI){} }\n"
"              }, d3);\n"
"            };\n"
"            if (k === 'fetch') return function(u2, o2){ try { print('[FRAME-FETCH] ' + String(u2).slice(0, 120)); } catch(pz2){} return g.fetch(u2, o2); };\n"
"            if (k === 'window' || k === 'self' || k === 'globalThis' || k === 'frames') return g;\n"
"            if (!(k in g)){ if (!missLog[k]){ missLog[k] = 1; try { print('[FRAME-MISS] ' + k); } catch(peM){} } return undefined; }\n"
"            if (g.__nfuDumpFrame && _wrapNames[k]){ if (!_wrapCache[k]) _wrapCache[k] = _nfuMissWrap(k, g[k]); return _wrapCache[k]; }\n"
"            return g[k];\n"
"          }, set: function(t, k, v){ g[k] = v; return true; }, has: function(t, k){ return true; } });\n"
"          // Route through __nfuEvalGlobal: the page's worker budget is often\n"
"          // already exhausted by the time the challenge iframe starts (bloxd\n"
"          // burns most of it on game init), and an interrupted eval silently\n"
"          // kills the challenge. argv[2] grants the frame its own window.\n"
"          g.__nfuFrameScope = scopeProxy;\n"
"          // \"not a function (got undefined) at call (native)\": the callee was a\n"
"          // bare undefined value handed to Function.prototype.call/apply, which\n"
"          // loses the source location. Log the offender's stack once so the\n"
"          // property that came back empty can be read off the dump.\n"
"          if (!g.__nfuCallHooked && g.__nfuDumpFrame){\n"
"            g.__nfuCallHooked = 1;\n"
"            var _oc = Function.prototype.call, _oa = Function.prototype.apply;\n"
"            Function.prototype.call = function(){\n"
"              if (typeof this !== 'function' && !g.__nfuCallBadN){\n"
"                g.__nfuCallBadN = 1;\n"
"                try { print('[CALL-BAD] this=' + String(this) + ' || ' + String((new Error()).stack || '').slice(0, 700)); } catch(pcb){}\n"
"              }\n"
"              return Reflect.apply(_oc, this, arguments);\n"
"            };\n"
"            Function.prototype.apply = function(){\n"
"              if (typeof this !== 'function' && !g.__nfuApplyBadN){\n"
"                g.__nfuApplyBadN = 1;\n"
"                try { print('[APPLY-BAD] this=' + String(this) + ' || ' + String((new Error()).stack || '').slice(0, 700)); } catch(pab){}\n"
"              }\n"
"              // Reflect.apply, not _oa.apply: the latter resolves through the\n"
"              // wrapper we are installing and recurses until stack overflow.\n"
"              return Reflect.apply(_oa, this, arguments);\n"
"            };\n"
"          }\n"
"          var wrapped = 'with(globalThis.__nfuFrameScope){\\n' + code + '\\n}';\n"
"          __nfuEvalGlobal(wrapped, url, 120);\n"
"          // The challenge's string table (a0/base64-ish) is the only way to\n"
"          // learn which host member a failing call wanted: wrap it after the\n"
"          // eval so the names decoded at call time (i.e. right before the\n"
"          // throw) are visible in plain text.\n"
"          try {\n"
"            if (g.__nfuDumpFrame && typeof g.a0 === 'function' && !g.a0.__nfuWrapped){\n"
"              var _a0 = g.a0, _a0n = 0;\n"
"              g.a0 = function(s9){\n"
"                var r9 = _a0.apply(this, arguments);\n"
"                if (_a0n < 200){ _a0n++; try { print('[CF-NAME] ' + String(s9) + ' -> ' + String(r9)); } catch(pn){} }\n"
"                return r9;\n"
"              };\n"
"              g.a0.__nfuWrapped = 1;\n"
"            }\n"
"          } catch(pa0){}\n"
"          g.__nfuFrameScope = null;\n"
"          try { print('[FRAME-EVAL-OK] ' + si); } catch(pz8){}\n"
"          frameDoc.currentScript = null;\n"
"        } catch(e6){ frameDoc.currentScript = null; try { print('[FRAME-SCRIPT-ERR] ' + (e6 && e6.message ? e6.message : e6) + ' ||| ' + (e6 && e6.stack ? e6.stack : '')); } catch(pe3){} }\n"
"      }\n"
"      n._nfuFrameLoading = 0; n._nfuFrameLoaded = 1;\n"
"      nfuFireScriptEvent(n, 'load');\n"
"    });\n"
"  }\n"
"  P.appendChild = function(c){ if (!c) return c; if (c._parent) c._parent.removeChild(c); c._parent = this; this._children.push(c); dirty(); if (c._tag === 'script') nfuLoadScript(c); else if (c._tag === 'link') nfuLoadLink(c); else if (c._tag === 'iframe') nfuLoadFrame(c); return c; };\n"
"  P.insertBefore = function(c, ref){ if (!c) return c; if (c._parent) c._parent.removeChild(c); var i = ref ? this._children.indexOf(ref) : -1; if (i < 0) i = this._children.length; c._parent = this; this._children.splice(i, 0, c); dirty(); if (c._tag === 'script') nfuLoadScript(c); else if (c._tag === 'link') nfuLoadLink(c); else if (c._tag === 'iframe') nfuLoadFrame(c); return c; };\n"
"  P.removeChild = function(c){ var i = this._children.indexOf(c); if (i >= 0){ this._children.splice(i, 1); c._parent = null; dirty(); } return c; };\n"
"  P.replaceChild = function(n, o){ var i = this._children.indexOf(o); if (i >= 0){ if (n._parent) n._parent.removeChild(n); this._children[i] = n; n._parent = this; o._parent = null; dirty(); } return o; };\n"
"  P.remove = function(){ if (this._parent) this._parent.removeChild(this); };\n"
"  // Minimal ShadowRoot: Turnstile (and many widgets) attachShadow({mode}) a\n"
"  // closed root and inject the challenge iframe into it. A plain container\n"
"  // node is enough — the widget UI is never shown, the token arrives via\n"
"  // postMessage callbacks.\n"
"  // NEFU-FIX: attachShadow used to mint a fresh root on every call. Real DOM returns\n"
"  // the existing root, so a second attachShadow() is a no-op. Mistakenly clobbering it\n"
"  // left the widget's nodes in the first root while every later lookup searched an\n"
"  // empty second one - exactly the `findById(...)` empty-shadow miss seen in the\n"
"  // Turnstile frame (and the widget tree is built once, so nothing ever re-created it).\n"
"  P.attachShadow = function(opts){ var md = (opts && opts.mode) || 'open';\n"
"    if (this._shadowRoot && this._shadowRoot._shadowMode === md) return this._shadowRoot;\n"
"    var s = new Node('#shadow-root'); s._shadowHost = this; s._shadowMode = md; this._shadowRoot = s; return s; };\n"
"  Object.defineProperty(P, 'shadowRoot', { get: function(){ return (this._shadowRoot && this._shadowRoot._shadowMode !== 'closed') ? this._shadowRoot : null; } , configurable: true});\n"
"  P.setAttribute = function(k, v){ k = String(k); this._attrs[k] = String(v); dirty();\n"
"    // NEFU-DIAG: the challenge registers its nodes as id=\"_<24 hex>\" and later\n"
"    // fetches them back by that id. The property setter was already instrumented;\n"
"    // setAttribute('id', ...) was not, so a miss here would look like 'the tree\n"
"    // was never built'. Log both to tell those two situations apart.\n"
"    if (g.__nfuDumpFrame && k === 'id' && typeof v === 'string' && v.charAt(0) === '_' && v.length > 16){\n"
"      g.__nfuAttrIdN = (g.__nfuAttrIdN || 0) + 1;\n"
"      if (g.__nfuAttrIdN < 24) try { print('[ID-ATTR] ' + this._tag + ' ' + v); } catch(ia1){}\n"
"    }\n"
"    // Turnstile creates its challenge iframe, appends it into the shadow\n"
"    // root, and only then sets src via setAttribute — which never went\n"
"    // through the property setter, so the frame never loaded.\n"
"    if (k === 'src' && String(v)){ if (this._tag === 'iframe') nfuLoadFrame(this); else if (this._tag === 'script') nfuLoadScript(this); else if (this._tag === 'link') nfuLoadLink(this); } };\n"
"  P.getAttribute = function(k){ return this._attrs[String(k)] !== undefined ? this._attrs[String(k)] : null; };\n"
"  P.removeAttribute = function(k){ delete this._attrs[String(k)]; dirty(); };\n"
"  // jQuery 1.10 attr()/val() fall back to nodeHook which calls\n"
"  // elem.getAttributeNode(name) on every non-bool attribute — without it\n"
"  // every .attr() call on the shim throws and chains silently die.\n"
"  P.getAttributeNode = function(k){ var v = this._attrs[String(k)]; if (v === undefined) return null; return { nodeName: String(k), nodeValue: v, name: String(k), value: String(v), specified: true }; };\n"
"  P.setAttributeNode = function(a){ if (a && a.name !== undefined){ this._attrs[String(a.name)] = String(a.value !== undefined ? a.value : ''); dirty(); } return null; };\n"
"  P.removeAttributeNode = function(a){ if (a && a.name !== undefined){ delete this._attrs[String(a.name)]; dirty(); } return null; };\n"
"  P.hasAttribute = function(k){ return this._attrs[String(k)] !== undefined; };\n"
"  P.addEventListener = function(t, f, c){ (this._events[t] = this._events[t] || []).push(f); };\n"
"  P.removeEventListener = function(t, f){ var a = this._events[t]; if (a){ var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } };\n"
"  P.dispatchEvent = function(ev){ if (!ev) return true; var a = this._events[ev.type] || []; var i; for (i=0;i<a.length;i++) a[i].call(this, ev); return true; };\n"
"  P.cloneNode = function(deep){\n"
"    var n = new Node(this._tag);\n"
"    n._text = this._text; n._inner = this._inner;\n"
"    for (var k in this._attrs) n._attrs[k] = this._attrs[k];\n"
"    for (var s in this._style) n._style[s] = this._style[s];\n"
"    if (deep){ var i; for (i=0;i<this._children.length;i++){ var c = this._children[i].cloneNode(true); c._parent = n; n._children.push(c); } }\n"
"    return n;\n"
"  };\n"
"  P.getElementsByTagName = function(t){ return byTag(this, t, []); };\n"
"  P.getElementsByClassName = function(c){ return byClass(this, c, []); };\n"
"  P.querySelectorAll = function(s){ return qselAll(this, s); };\n"
"  P.querySelector = function(s){ var a = qselAll(this, s); return a[0] || null; };\n"
"  // ---- element behaviour stubs (SPA frameworks probe these constantly) ----\n"
"  P.getBoundingClientRect = function(){ var w = nfuBoxW(this, 0), h = nfuBoxH(this, 0);\n"
"    // A game canvas that measures narrower than its containing window cannot show the\n"
"    // world even when rendering works: noa sizes its viewport from this box. Log the\n"
"    // first few canvas measurements (CSS box vs window) so the source of a shrunken\n"
"    // canvas is identifiable instead of guessed at.\n"
"    try { if (this._tag === 'canvas' || String(this.tagName || '').toLowerCase() === 'canvas') { g.__nfuCvMeas = (g.__nfuCvMeas || 0) + 1; if (g.__nfuCvMeas <= 6) print('[CVMEAS] id=' + String(this._attrs && this._attrs.id || '') + ' box=' + w + 'x' + h + ' win=' + (g.innerWidth|0) + 'x' + (g.innerHeight|0) + ' attr=' + String(this._attrs && this._attrs.width || '') + 'x' + String(this._attrs && this._attrs.height || '')); } } catch(pe){}\n"
"    return { x: 0, y: 0, top: 0, left: 0, right: w, bottom: h, width: w, height: h }; };\n"
"  P.getClientRects = function(){ return [this.getBoundingClientRect()]; };\n"
"  P.focus = function(){ g.document.activeElement = this; };\n"
"  P.blur = function(){ if (g.document.activeElement === this) g.document.activeElement = g.document.body; };\n"
"  P.click = function(){ this.dispatchEvent({ type: 'click', target: this, currentTarget: this, bubbles: true, cancelable: true, defaultPrevented: false, clientX: 0, clientY: 0, preventDefault: function(){ this.defaultPrevented = true; }, stopPropagation: function(){} }); };\n"
"  P.scrollIntoView = function(){};\n"
"  // pointer lock: first-person games capture the cursor and read movementX/Y;\n"
"  // the browser keeps feeding mousemove so the camera keeps turning\n"
"  P.requestPointerLock = function(){ g.document.pointerLockElement = this; g.__nfuLocked = this; };\n"
"  P.releasePointerLock = function(){ g.document.pointerLockElement = null; g.__nfuLocked = null; };\n"
"  // Fullscreen API. bloxd only offers its \"Enter Bloxd\" entry when the browser\n"
"  // reports fullscreen support, so these must exist and succeed.\n"
"  P.requestFullscreen = function(){\n"
"    var el = this;\n"
"    g.document.fullscreenElement = el;\n"
"    g.document.webkitFullscreenElement = el;\n"
"    g.__nfuFullscreen = el;\n"
"    try { print('[FS] enter ' + String(el._tag || el.nodeName)); } catch(e){}\n"
"    setTimeout(function(){\n"
"      try { g.document.dispatchEvent({ type: 'fullscreenchange', target: g.document, bubbles: false, cancelable: false }); } catch(e){}\n"
"      try { el.dispatchEvent({ type: 'fullscreenchange', target: el, bubbles: true, cancelable: false }); } catch(e){}\n"
"      try { if (typeof g.onresize === 'function') g.onresize({ type: 'resize' }); } catch(e){}\n"
"    }, 0);\n"
"    return Promise.resolve();\n"
"  };\n"
"  P.webkitRequestFullscreen = P.requestFullscreen;\n"
"  P.webkitRequestFullScreen = P.requestFullscreen;\n"
"  P.mozRequestFullScreen = P.requestFullscreen;\n"
"  P.msRequestFullscreen = P.requestFullscreen;\n"
"  P.requestFullscreen = P.requestFullscreen;\n"
"  P.setPointerCapture = function(){};\n"
"  P.releasePointerCapture = function(){};\n"
"  P.hasPointerCapture = function(){ return true; };\n"
"  P.scrollTo = function(){};\n"
"  P.scrollBy = function(){};\n"
"  P.animate = function(){ return { finished: Promise.resolve(), cancel: function(){}, play: function(){}, pause: function(){}, onfinish: null, addEventListener: function(){} }; };\n"
"  P.contains = function(n){ while (n){ if (n === this) return true; n = n._parent; } return false; };\n"
"  P.closest = function(sel){ var n = this; while (n && n._tag){ if (n.matches && n.matches(sel)) return n; n = n._parent; } return null; };\n"
"  P.matches = function(sel){ sel = String(sel).replace(/^\\s+|\\s+$/g, ''); if (sel.charAt(0) === '#') return this._attrs['id'] === sel.slice(1); if (sel.charAt(0) === '.') { var c = ' ' + (this._attrs['class'] || '') + ' '; return c.indexOf(' ' + sel.slice(1) + ' ') >= 0; } return this.nodeName === sel.toUpperCase(); };\n"
"  P.append = function(){ for (var i = 0; i < arguments.length; i++){ var a = arguments[i]; this.appendChild(typeof a === 'string' ? g.document.createTextNode(a) : a); } };\n"
"  P.prepend = function(){ for (var i = arguments.length - 1; i >= 0; i--){ var a = arguments[i]; this.insertBefore(typeof a === 'string' ? g.document.createTextNode(a) : a, this.firstChild); } };\n"
"  P.replaceChildren = function(){ this._inner = null; this._children.length = 0; for (var i = 0; i < arguments.length; i++){ var a = arguments[i]; this.appendChild(typeof a === 'string' ? g.document.createTextNode(a) : a); } dirty(); };\n"
"  // replaceWith: swap this node for the given nodes in its parent. bloxd's\n"
"  // renderer switch (Lite <-> WebGL) ends with\n"
"  //   canvas.replaceWith(canvas.cloneNode(false))\n"
"  // and the missing method threw \"not a function (got undefined)\", which\n"
"  // aborted \"Failed to initialise game engine\" before the 3D world ever\n"
"  // started. Standard semantics: detach self, insert the arguments at its\n"
"  // old index, strings become text nodes.\n"
"  P.replaceWith = function(){ var p = this._parent; if (!p) return; var i = p._children.indexOf(this); if (i < 0) return; p._children.splice(i, 1); this._parent = null; for (var k = 0; k < arguments.length; k++){ var a = arguments[k]; if (typeof a === 'string') a = g.document.createTextNode(a); if (!a || a === this) continue; if (a._parent) a._parent.removeChild(a); a._parent = p; p._children.splice(i + k, 0, a); } dirty(); };\n"
"  // insertAdjacentHTML: parse the HTML fragment into real child nodes\n"
"  // (a naive _inner string concat used to silently REPLACE all real\n"
"  // children at serialize time, hiding framework-rendered UI and leaking\n"
"  // inline-script source as page text). Mini parser: tags, attributes,\n"
"  // nesting via a stack, void elements, script/style raw bodies.\n"
"  var __voidTags = { br:1, img:1, hr:1, input:1, meta:1, link:1, source:1, area:1, base:1, col:1, embed:1, track:1, wbr:1 };\n"
"  var __ents = { amp:'&', lt:'<', gt:'>', quot:'\"', apos:'\\'', nbsp:' ', copy:'\\u00A9', reg:'\\u00AE', mdash:'\\u2014', ndash:'\\u2013', hellip:'\\u2026', laquo:'\\u00AB', raquo:'\\u00BB', middot:'\\u00B7' };\n"
"  function nfuDecodeEnt(s){\n"
"    if (s.indexOf('&') < 0) return s;\n"
"    return s.replace(/&(#x?[0-9a-fA-F]+|[a-zA-Z]+);/g, function(m, code){\n"
"      if (code.charAt(0) === '#'){\n"
"        var cp;\n"
"        if (code.charAt(1) === 'x' || code.charAt(1) === 'X') cp = parseInt(code.slice(2), 16);\n"
"        else cp = parseInt(code.slice(1), 10);\n"
"        if (!(cp > 0)) return m;\n"
"        try { return String.fromCodePoint(cp); } catch(e) { return m; }\n"
"      }\n"
"      var v = __ents[code];\n"
"      return (v === undefined) ? m : v;\n"
"    });\n"
"  }\n"
"  function parseHTMLInto(parent, html){\n"
"    html = String(html);\n"
"    var stack = [parent], i = 0, n = html.length;\n"
"    function top(){ return stack[stack.length - 1]; }\n"
"    while (i < n){\n"
"      var lt = html.indexOf('<', i);\n"
"      if (lt < 0){ if (i < n) top().appendChild(g.document.createTextNode(nfuDecodeEnt(html.slice(i)))); break; }\n"
"      if (lt > i) top().appendChild(g.document.createTextNode(nfuDecodeEnt(html.slice(i, lt))));\n"
"      if (html.charAt(lt + 1) === '!'){\n"
"        if (html.substr(lt, 4) === '<!--'){\n"
"          var ce2 = html.indexOf('-->', lt);\n"
"          if (ce2 < 0) break;\n"
"          var cm = new Node('#comment');\n"
"          cm._text = html.slice(lt + 4, ce2);\n"
"          top().appendChild(cm);\n"
"          i = ce2 + 3;\n"
"        } else {\n"
"          var ce = html.indexOf('>', lt);\n"
"          if (ce < 0) break;\n"
"          i = ce + 1;\n"
"        }\n"
"        continue;\n"
"      }\n"
"      var close = html.charAt(lt + 1) === '/';\n"
"      var ge = html.indexOf('>', lt);\n"
"      if (ge < 0) break;\n"
"      var inner = html.slice(lt + (close ? 2 : 1), ge);\n"
"      var tag = (inner.match(/^[a-zA-Z][a-zA-Z0-9]*/) || [''])[0].toLowerCase();\n"
"      if (!tag){ i = ge + 1; continue; }\n"
"      if (close){\n"
"        for (var si = stack.length - 1; si > 0; si--){\n"
"          if (stack[si]._tag === tag){ stack.length = si; break; }\n"
"        }\n"
"      } else {\n"
"        var el = g.document.createElement(tag);\n"
"        if (tag === 'script') el._nfuNoExec = 1;   // innerHTML scripts never run (real DOM)\n"
"        var rest = inner.slice(tag.length);\n"
"        parseAttrs(el, rest);\n"
"        top().appendChild(el);\n"
"        if (__voidTags[tag]){\n"
"          /* void: no body */\n"
"        } else if (tag === 'script' || tag === 'style'){\n"
"          var closeTag = '</' + tag;\n"
"          var ci2 = html.toLowerCase().indexOf(closeTag, ge);\n"
"          if (ci2 < 0) ci2 = n;\n"
"          if (ci2 > ge + 1) el._inner = html.slice(ge + 1, ci2);\n"
"          var cgt = html.indexOf('>', ci2);\n"
"          i = (cgt < 0 ? n : cgt + 1);\n"
"          stack.length = 1;\n"
"          continue;\n"
"        } else {\n"
"          stack.push(el);\n"
"        }\n"
"      }\n"
"      i = ge + 1;\n"
"    }\n"
"    dirty();\n"
"  }\n"
"  P.insertAdjacentHTML = function(pos, html){\n"
"    // all positions approximated as a child append (browser positions for\n"
"    // beforebegin/afterend target the parent; rare in framework code)\n"
"    parseHTMLInto(this, html);\n"
"  };\n"
"\n"
"  // ---- box metrics --------------------------------------------------------\n"
"  // bloxd's carousel decides how many cards fit on a page from the parent's\n"
"  // clientWidth, and writes the resulting percentage into the card's inline\n"
"  // style. The old hardcoded 300 made it pick six columns for a 219px phone\n"
"  // viewport, so every page held the whole list and the home page grew to\n"
"  // 40000px. Derive the width from the inline percentage chain, rooted at the\n"
"  // real viewport width.\n"
"  function nfuPctLen(t){ if (!t) return -1; t = String(t).trim(); if (t.slice(-1) !== '%') return -1; var v = parseFloat(t); return isNaN(v) ? -1 : v; }\n"
"  function nfuBoxW(el, depth){\n"
"    if (!el || !el._tag || el._tag === '#document') return (g.innerWidth || 800);\n"
"    if ((depth || 0) > 24) return (g.innerWidth || 800);\n"
"    var st = (el.style && el.style.width) || (el._attrs && el._attrs['style'] ? (String(el._attrs['style']).match(/width\\s*:\\s*([^;]+)/) || [])[1] : null);\n"
"    var pct = nfuPctLen(st);\n"
"    if (pct >= 0) {\n"
"      var par = el._parent;\n"
"      var base = par ? nfuBoxW(par, (depth || 0) + 1) : (g.innerWidth || 800);\n"
"      return Math.round(base * pct / 100);\n"
"    }\n"
"    if (st && /px$/.test(String(st).trim())) return Math.round(parseFloat(st));\n"
"    var par2 = el._parent;\n"
"    return par2 ? nfuBoxW(par2, (depth || 0) + 1) : (g.innerWidth || 800);\n"
"  }\n"
"  function nfuBoxH(el, depth){\n"
"    if (!el || !el._tag || el._tag === '#document') return (g.innerHeight || 600);\n"
"    if ((depth || 0) > 24) return (g.innerHeight || 600);\n"
"    var st = (el.style && el.style.height) || (el._attrs && el._attrs['style'] ? (String(el._attrs['style']).match(/height\\s*:\\s*([^;]+)/) || [])[1] : null);\n"
"    var pct = nfuPctLen(st);\n"
"    if (pct >= 0) {\n"
"      var par = el._parent;\n"
"      var base = par ? nfuBoxH(par, (depth || 0) + 1) : (g.innerHeight || 600);\n"
"      return Math.round(base * pct / 100);\n"
"    }\n"
"    if (st && /px$/.test(String(st).trim())) return Math.round(parseFloat(st));\n"
"    var par3 = el._parent;\n"
"    return par3 ? nfuBoxH(par3, (depth || 0) + 1) : (g.innerHeight || 600);\n"
"  }\n"
"  Object.defineProperty(P, 'dataset', { get: function(){ var d = {}, k; for (k in this._attrs){ if (k.indexOf('data-') === 0) d[k.slice(5).replace(/-([a-z])/g, function(m, c){ return c.toUpperCase(); })] = this._attrs[k]; } return d; } , configurable: true});\n"
"  Object.defineProperty(P, 'offsetWidth', { get: function(){ return nfuBoxW(this, 0); } , configurable: true});\n"
"  Object.defineProperty(P, 'offsetHeight', { get: function(){ return nfuBoxH(this, 0); } , configurable: true});\n"
"  Object.defineProperty(P, 'offsetTop', { get: function(){ return 0; } , configurable: true});\n"
"  Object.defineProperty(P, 'offsetLeft', { get: function(){ return 0; } , configurable: true});\n"
"  Object.defineProperty(P, 'offsetParent', { get: function(){ return this._parent; } , configurable: true});\n"
"  Object.defineProperty(P, 'clientWidth', { get: function(){ return nfuBoxW(this, 0); } , configurable: true});\n"
"  Object.defineProperty(P, 'clientHeight', { get: function(){ var h = nfuBoxH(this, 0); return h; } , configurable: true});\n"
"  Object.defineProperty(P, 'scrollWidth', { get: function(){ return nfuBoxW(this, 0); } , configurable: true});\n"
"  Object.defineProperty(P, 'scrollHeight', { get: function(){ return nfuBoxH(this, 0) || 24; } , configurable: true});\n"
"  Object.defineProperty(P, 'scrollTop', { get: function(){ return this._st || 0; }, set: function(v){ this._st = v; dirty(); } , configurable: true});\n"
"  Object.defineProperty(P, 'scrollLeft', { get: function(){ return 0; }, set: function(){} , configurable: true});\n"
"  Object.defineProperty(P, 'value', { get: function(){ return this._val !== undefined ? this._val : (this._attrs['value'] || ''); }, set: function(v){ this._val = v; dirty(); } , configurable: true});\n"
"  Object.defineProperty(P, 'checked', { get: function(){ return !!this._checked || this._attrs['checked'] !== undefined || this._attrs['selected'] !== undefined; }, set: function(v){ this._checked = !!v; if (!v) { delete this._attrs['checked']; delete this._attrs['selected']; } dirty(); } , configurable: true});\n"
"  Object.defineProperty(P, 'selected', { get: function(){ return this.checked; }, set: function(v){ this.checked = v; } , configurable: true});\n"
"  Object.defineProperty(P, 'disabled', { get: function(){ return this._attrs['disabled'] !== undefined; }, set: function(v){ if (v) this._attrs['disabled'] = ''; else delete this._attrs['disabled']; dirty(); } , configurable: true});\n"
"  Object.defineProperty(P, 'hidden', { get: function(){ return this._attrs['hidden'] !== undefined; }, set: function(v){ if (v) this._attrs['hidden'] = ''; else delete this._attrs['hidden']; dirty(); } , configurable: true});\n"
"  Object.defineProperty(P, 'src', { get: function(){ return this._attrs['src'] || ''; }, set: function(v){ this._attrs['src'] = String(v); dirty(); if (this._tag === 'script') nfuLoadScript(this); else if (this._tag === 'iframe') nfuLoadFrame(this); } , configurable: true});\n"
"  Object.defineProperty(P, 'href', { get: function(){ return this._attrs['href'] || ''; }, set: function(v){ this._attrs['href'] = String(v); dirty(); if (this._tag === 'link') nfuLoadLink(this); } , configurable: true});\n"
"  Object.defineProperty(P, 'type', { get: function(){ return this._attrs['type'] || ''; }, set: function(v){ this._attrs['type'] = String(v); dirty(); } , configurable: true});\n"
"  function NfuDoc(){}\n"
"  var bodyNode = new Node('body');\n"
"  var headNode = new Node('head');\n"
"  var htmlNode = new Node('html');\n"
"  htmlNode._children.push(headNode); headNode._parent = htmlNode;\n"
"  htmlNode._children.push(bodyNode); bodyNode._parent = htmlNode;\n"
"  var byId = {};\n"
"  var oldDoc = g.document;\n"
"  // nodeType 9 is load-bearing: Sizzle/jQuery's setDocument() refuses any\n"
"  // candidate whose nodeType !== 9 and leaves its closure `document` undefined\n"
"  // forever — assert() then throws OUTSIDE its try block and the whole\n"  
"  // jQuery 1.x bundle dies ($ never defined; baidu's init chain broke on it).\n"
"  NfuDoc.prototype.nodeType = 9;\n"
"  NfuDoc.prototype.nodeName = '#document';\n"
"  NfuDoc.prototype.ownerDocument = null;\n"
"  NfuDoc.prototype.createElement = function(tag){ return new Node(String(tag).toLowerCase()); };\n"
"  NfuDoc.prototype.createElementNS = function(ns, tag){ return new Node(String(tag).toLowerCase()); };\n"
"  NfuDoc.prototype.createTextNode = function(s){ var n = new Node('#text'); n._text = String(s); return n; };\n"
"  NfuDoc.prototype.createComment = function(s){ var n = new Node('#comment'); n._text = String(s); return n; };\n"
"  NfuDoc.prototype.createAttribute = function(name){ return { nodeName: String(name), nodeValue: '', name: String(name), value: '', specified: true }; };\n"
"  NfuDoc.prototype.createEvent = function(t){ return { type: String(t), target: null }; };\n"
"  NfuDoc.prototype.write = function(s){ if (oldDoc && oldDoc.write) oldDoc.write(s); };\n"
"  NfuDoc.prototype.getElementById = function(id){\n"
"    if (byId[id]) return byId[id];\n"
"    var n = new Node('div');\n"
"    n._attrs['id'] = String(id);\n"
"    byId[id] = n;\n"
"    bodyNode._children.push(n); n._parent = bodyNode;   // auto-attach mount points\n"
"    return n;\n"
"  };\n"
"  NfuDoc.prototype.getElementsByTagName = function(t){ return byTag(bodyNode, t, []); };\n"
"  NfuDoc.prototype.getElementsByClassName = function(c){ return byClass(bodyNode, c, []); };\n"
"  NfuDoc.prototype.querySelector = function(s){ var a = qselAll(bodyNode, s);\n"
"    // NEFU-DIAG: the challenge addresses its own nodes as '#_<24 hex>'. If the\n"
"    // PAGE document sees those selectors, the frame script resolved `document` to\n"
"    // the page instead of its own frame doc - the exact mismatch that produced the\n"
"    // null derefs in bd34/bd37.\n"
"    if (g.__nfuDumpFrame && typeof s === 'string' && s.charAt(0) === '#' && s.length > 16 && g.__nfuPageQsN !== 0){\n"
"      g.__nfuPageQsN = (g.__nfuPageQsN || 0) + (a.length ? 0 : 1);\n"
"      if (g.__nfuPageQsN < 30) try { print('[PAGE-QS] ' + s + (a.length ? ' =hit' : ' =MISS')); } catch(pq1){}\n"
"    }\n"
"    return a[0] || null; };\n"
"  NfuDoc.prototype.querySelectorAll = function(s){ return qselAll(htmlNode, s); };\n"
"  Object.defineProperty(NfuDoc.prototype, 'body', { get: function(){ return bodyNode; } , configurable: true});\n"
"  Object.defineProperty(NfuDoc.prototype, 'documentElement', { get: function(){ return htmlNode; } , configurable: true});\n"
"  Object.defineProperty(NfuDoc.prototype, 'head', { get: function(){ return headNode; } , configurable: true});\n"
"  Object.defineProperty(NfuDoc.prototype, 'scripts', { get: function(){ return byTag(htmlNode, 'script', []); } , configurable: true});\n"
"  Object.defineProperty(NfuDoc.prototype, 'title', { get: function(){ return oldDoc ? oldDoc.title : ''; }, set: function(v){ if (oldDoc) oldDoc.title = String(v); } , configurable: true});\n"
"  Object.defineProperty(NfuDoc.prototype, 'URL', { get: function(){ return oldDoc ? oldDoc.URL : ''; } , configurable: true});\n"
"  // document.cookie as a real (per-document, in-memory) jar. bloxd's anonymous\n"
"  // sign-in reads metrics cookies off the document (getMetricsCookies), so the\n"
"  // getter must return \"k=v; k2=v2\" - a raw echo of the last assignment would\n"
"  // hand back attributes ('SID=x; path=/') and trip its parser.\n"
"  var _cookieJar = {};\n"
"  Object.defineProperty(NfuDoc.prototype, 'cookie', {\n"
"    get: function(){ var s = '', k; for (k in _cookieJar){ if (s) s += '; '; s += k + '=' + _cookieJar[k]; } return s; },\n"
"    set: function(v){ try {\n"
"      var parts = String(v).split(';'), head = parts[0] || '', eq = head.indexOf('=');\n"
"      if (eq <= 0) return;\n"
"      var k = head.substring(0, eq).replace(/^\\s+|\\s+$/g, ''), val = head.substring(eq + 1), del = false, i;\n"
"      for (i = 1; i < parts.length; i++){ var p = parts[i].toLowerCase();\n"
"        if (p.indexOf('max-age=0') >= 0 || (p.indexOf('expires=') >= 0 && p.indexOf('1970') >= 0)) del = true; }\n"
"      if (del) { delete _cookieJar[k]; return; }\n"
"      _cookieJar[k] = val;\n"
"      // Trace cookie writes: bloxd's anonymous identity lands here, and if the write\n"
"      // never happens the game-server requests go out without any credential.\n"
"      try { print('[COOKIE-SET] ' + k + ' len=' + String(val).length); } catch(cw){}\n"
"    } catch(e){} },\n"
"    configurable: true});\n"
"  // Seed the jar from a real browser session on this machine when available\n"
"  // (NEFU_COOKIE_FILE). The site's analytics visitor id lives in these values\n"
"  // and the game servers authenticate the WebSocket upgrade against it, so\n"
"  // without the seed every room join ends in Colyseus error 4215 onAuth.\n"
"  try {\n"
"    if (typeof g.__nfuCookieSeed === 'function') {\n"
"      var _seedS = String(g.__nfuCookieSeed() || '');\n"
"      if (_seedS) {\n"
"        var _sp = _seedS.split(';'), _sn = 0, _si;\n"
"        for (_si = 0; _si < _sp.length; _si++) {\n"
"          var _seg = _sp[_si].replace(/^\\s+|\\s+$/g, '');\n"
"          if (!_seg) continue;\n"
"          var _eq = _seg.indexOf('=');\n"
"          if (_eq <= 0) continue;\n"
"          _cookieJar[_seg.substring(0, _eq)] = _seg.substring(_eq + 1);\n"
"          _sn++;\n"
"        }\n"
"        try { print('[CK-SEED] n=' + _sn + ' chars=' + _seedS.length); } catch(_se3){}\n"
"      }\n"
"    }\n"
"  } catch(_se2){}\n"
"  NfuDoc.prototype._events = {};\n"
"  NfuDoc.prototype.addEventListener = function(t, f){\n"
"    if (typeof f !== 'function') return;\n"
"    if (!Object.prototype.hasOwnProperty.call(this, '_events')) this._events = {};\n"
"    var a = this._events[t] || (this._events[t] = []);\n"
"    if (a.indexOf(f) < 0) a.push(f);\n"
"  };\n"
"  NfuDoc.prototype.removeEventListener = function(t, f){ var a = this._events[t]; if (a){ var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } };\n"
"  NfuDoc.prototype.dispatchEvent = function(ev){ if (!ev) return true; if (!ev.target) ev.target = this; g.__nfuFire(ev, this); return !ev.defaultPrevented; };\n"
"  NfuDoc.prototype.createDocumentFragment = function(){ return new Node('#fragment'); };\n"
"  NfuDoc.prototype.createRange = function(){ return { setStart: function(){}, setEnd: function(){}, selectNodeContents: function(){}, collapse: function(){}, getBoundingClientRect: function(){ return { x:0,y:0,top:0,left:0,right:0,bottom:0,width:0,height:0 }; }, getClientRects: function(){ return []; }, createContextualFragment: function(){ return new Node('#fragment'); }, insertNode: function(){}, detach: function(){} }; };\n"
"  NfuDoc.prototype.elementFromPoint = function(x, y){ return bodyNode; };\n"
"  NfuDoc.prototype.elementsFromPoint = function(x, y){ return [bodyNode]; };\n"
"  NfuDoc.prototype.getElementsByName = function(){ return []; };\n"
"  NfuDoc.prototype.hasFocus = function(){ return true; };\n"
"  NfuDoc.prototype.execCommand = function(){ return true; };\n"
"  NfuDoc.prototype.createTreeWalker = function(root){ return { root: root || bodyNode, currentNode: root || bodyNode, nextNode: function(){ return null; }, previousNode: function(){ return null; } }; };\n"
"  // Shared attribute scanner. HTML allows valueless (\"boolean\") attributes\n"
"  // (checked / selected / disabled / required / multiple / autofocus); the\n"
"  // old name=\"value\"-only regex dropped them, so every seeded checkbox looked\n"
"  // unticked and <option selected> lost its state.\n"
"  function parseAttrs(el, rest){\n"
"    var ai = 0, rl = rest.length;\n"
"    while (ai < rl){\n"
"      while (ai < rl && /[\\s\\/]/.test(rest.charAt(ai))) ai++;\n"
"      var nm = '';\n"
"      while (ai < rl && /[-a-zA-Z0-9_:.]/.test(rest.charAt(ai))) nm += rest.charAt(ai++);\n"
"      if (!nm){ ai++; continue; }\n"
"      while (ai < rl && /\\s/.test(rest.charAt(ai))) ai++;\n"
"      if (rest.charAt(ai) !== '='){ el._attrs[nm] = ''; continue; }\n"
"      ai++;\n"
"      while (ai < rl && /\\s/.test(rest.charAt(ai))) ai++;\n"
"      var q = rest.charAt(ai), v2 = '';\n"
"      if (q === '\"' || q === \"'\"){\n"
"        ai++;\n"
"        while (ai < rl && rest.charAt(ai) !== q) v2 += rest.charAt(ai++);\n"
"        ai++;\n"
"      } else {\n"
"        while (ai < rl && !/[\\s>]/.test(rest.charAt(ai))) v2 += rest.charAt(ai++);\n"
"      }\n"
"      el._attrs[nm] = nfuDecodeEnt(v2);\n"
"    }\n"
"  }\n"
"  function parseSeed(html){\n"
"    if (!html) return;\n"
"    var stack = [bodyNode];\n"
"    var i = 0, L = html.length;\n"
"    function pushText(s){ if (!s) return; var p = stack[stack.length-1]; if (!p) return; var tn = new Node('#text'); tn._text = s; p._children.push(tn); tn._parent = p; }\n"
"    while (i < L){\n"
"      var lt = html.indexOf('<', i);\n"
"      if (lt < 0){ pushText(html.substring(i)); break; }\n"
"      if (lt > i) pushText(html.substring(i, lt));\n"
"      var gt = html.indexOf('>', lt);\n"
"      if (gt < 0){ pushText(html.substring(lt)); break; }\n"
"      var raw = html.substring(lt + 1, gt);\n"
"      if (raw.charAt(0) === '/'){ if (stack.length > 1) stack.pop(); i = gt + 1; continue; }\n"
"      var selfClose = raw.charAt(raw.length - 1) === '/';\n"
"      var m = raw.match(/^[a-zA-Z][a-zA-Z0-9-]*/);\n"
"      if (!m){ i = gt + 1; continue; }\n"
"      var tag = m[0].toLowerCase();\n"
"      var n = new Node(tag);\n"
"      parseAttrs(n, raw.substring(m[0].length));   // values + boolean attributes\n"
"      if (n._attrs['id']) byId[n._attrs['id']] = n;\n"
"      var parent = stack[stack.length-1];\n"
"      parent._children.push(n); n._parent = parent;\n"
"      if (!selfClose && tag !== 'br' && tag !== 'hr' && tag !== 'img' && tag !== 'input' && tag !== 'meta' && tag !== 'link') stack.push(n);\n"
"      i = gt + 1;\n"
"    }\n"
"  }\n"
"  if (g.__nfuSeed) parseSeed(g.__nfuSeed);   // seed the shim DOM with static HTML\n"
"  var nd = new NfuDoc();\n"
"  // document.fonts: engines gate text/shader assets on fonts.ready, and a\n"
"  // missing object left that await pending forever (bloxd never left its\n"
"  // 'Loading required libraries' stage). An always-resolved set is enough -\n"
"  // layout here does not depend on real webfont metrics.\n"
"  if (!nd.fonts){\n"
"    nd.fonts = { ready: Promise.resolve(), status: 'loaded', add: function(){}, delete: function(){},\n"
"                 check: function(){ return true; }, load: function(){ return Promise.resolve([]); },\n"
"                 forEach: function(){}, addEventListener: function(){}, removeEventListener: function(){} };\n"
"  }\n"
"  g.document = nd;\n"
"  // Fullscreen state on the document side: bloxd reads fullscreenEnabled and\n"
"  // fullscreenElement to decide whether the \"Enter Bloxd\" entry may be shown.\n"
"  nd.fullscreenEnabled = true;\n"
"  nd.webkitFullscreenEnabled = true;\n"
"  nd.mozFullScreenEnabled = true;\n"
"  nd.msFullscreenEnabled = true;\n"
"  nd.fullscreenElement = null;\n"
"  nd.webkitFullscreenElement = null;\n"
"  nd.mozFullScreenElement = null;\n"
"  nd.exitFullscreen = function(){\n"
"    var was = nd.fullscreenElement;\n"
"    nd.fullscreenElement = null;\n"
"    nd.webkitFullscreenElement = null;\n"
"    nd.mozFullScreenElement = null;\n"
"    g.__nfuFullscreen = null;\n"
"    try { print('[FS] exit'); } catch(e){}\n"
"    setTimeout(function(){\n"
"      try { nd.dispatchEvent({ type: 'fullscreenchange', target: nd, bubbles: false, cancelable: false }); } catch(e){}\n"
"      try { if (was) was.dispatchEvent({ type: 'fullscreenchange', target: was, bubbles: true, cancelable: false }); } catch(e){}\n"
"    }, 0);\n"
"    return Promise.resolve();\n"
"  };\n"
"  nd.webkitExitFullscreen = nd.exitFullscreen;\n"
"  nd.mozCancelFullScreen = nd.exitFullscreen;\n"
"  nd.msExitFullscreen = nd.exitFullscreen;\n"
"  // FontFace: bloxd's setup news up `window.FontFace(name, url)` and awaits\n"
"  // `.load()` before adding it to document.fonts. Without the constructor the\n"
"  // whole initial-setup promise rejected and the world never started. A\n"
"  // resolved record is enough because layout here never measures webfonts.\n"
"  if (typeof g.FontFace !== 'function'){\n"
"    g.FontFace = function FontFace(family, source, descriptors){\n"
"      if (!(this instanceof g.FontFace)) return new g.FontFace(family, source, descriptors);\n"
"      this.family = String(family);\n"
"      this.source = String(source);\n"
"      this.descriptors = descriptors || {};\n"
"      this.status = 'unloaded';\n"
"      this.loaded = Promise.resolve(this);\n"
"    };\n"
"    g.FontFace.prototype.load = function(){ this.status = 'loaded'; return Promise.resolve(this); };\n"
"  }\n"
"  // console must exist before any page script compiles: bundles read it at\n"
"  // module scope (typeof checks are not universal) and a bare reference to an\n"
"  // undefined global throws ReferenceError, killing the whole bundle.\n"
"  if (typeof g.console === 'undefined' || !g.console || !g.console.log){\n"
"    g.console = { log: function(){ try { print(Array.prototype.join.call(arguments, ' ')); } catch(e){} },\n"
"                  warn: function(){ try { print(Array.prototype.join.call(arguments, ' ')); } catch(e){} },\n"
"                  error: function(){ try { print(Array.prototype.join.call(arguments, ' ')); } catch(e){} },\n"
"                  info: function(){ try { print(Array.prototype.join.call(arguments, ' ')); } catch(e){} },\n"
"                  debug: function(){}, trace: function(){}, assert: function(){},\n"
"                  count: function(){}, table: function(){}, dir: function(){},\n"
"                  group: function(){}, groupEnd: function(){}, groupCollapsed: function(){},\n"
"                  time: function(){}, timeEnd: function(){}, timeLog: function(){} };\n"
"  }\n"
"  // print() truncates each line at ~260 chars, which hides exactly the part of\n"
"  // an engine error we need (shader builder stack). Split long console output\n"
"  // into numbered 190-char segments so the whole message survives.\n"
"  if (g.console && !g.console.__nfuChunked) {\n"
"    var _ch = function(tag, orig){ return function(){\n"
"      var m; try { m = Array.prototype.join.call(arguments, ' '); } catch(e){ m = ''; }\n"
"      if (!m || m.length <= 190) { try { orig(m); } catch(e){} return; }\n"
"      var n = Math.ceil(m.length / 190);\n"
"      for (var i = 0; i < n; i++) {\n"
"        try { print('[CHK]' + tag + (i + 1) + '/' + n + ' ' + m.substr(i * 190, 190)); } catch(e){}\n"
"      }\n"
"    }; };\n"
"    var _mkl = function(orig){ return function(){ try { print(Array.prototype.join.call(arguments, ' ')); } catch(e){} }; };\n"
"    var _o1 = _mkl(function(v){ print(v); });\n"
"    g.console.log = _ch('L', function(v){ _o1.apply(null, [v]); });\n"
"    g.console.warn = _ch('W', function(v){ _o1.apply(null, [v]); });\n"
"    g.console.error = _ch('E', function(v){ _o1.apply(null, [v]); });\n"
"    g.console.__nfuChunked = true;\n"
"  }\n"
"  // The game leaves the play route microseconds after its engine banner, and\n"
"  // its own error UI is gone by the time the route change settles. 'Doing Game\n"
"  // Exited' is the platform hook the departure goes through, so dump the page\n"
"  // text at that exact log line to catch whatever the game wrote there.\n"
"  try {\n"
"    if (g.console && g.console.log && !g.console.__nfuExitHooked) {\n"
"      var _cl0 = g.console.log;\n"
"      g.console.log = function(){\n"
"        try {\n"
"          var _m = Array.prototype.join.call(arguments, ' ');\n"
"          if (_m && _m.indexOf('Game Exited') >= 0) {\n"
"            var _t = '';\n"
"            try { _t = String(g.document.body.innerText || '').replace(/\\s+/g, ' ').slice(0, 400); } catch(_e){}\n"
"            print('[EXIT-BODY] ' + _t);\n"
"            var _d = 0;\n"
"            try { _d = g.document.body.innerHTML.length; } catch(_e2){}\n"
"            print('[EXIT-DOM] len=' + _d);\n"
"          }\n"
"        } catch(_e3){}\n"
"        return _cl0.apply(null, arguments);\n"
"      };\n"
"      g.console.__nfuExitHooked = true;\n"
"    }\n"
"  } catch(_e4){}\n"
"  // The play route's engine setup dies inside a React DOM commit and the error\n"
"  // object carries an empty message, so the failing DOM call must name itself.\n"
"  // Wrap the plain Node prototype methods (enumerable own props) only: an\n"
"  // earlier pass that also redefined every accessor made the site's new main\n"
"  // bundle fail to evaluate outright (`invalid 'in' operand`), so accessors are\n"
"  // deliberately left untouched.\n"
"  try {\n"
"    if (typeof P === 'object' && !P.__nfuDomArmed) {\n"
"      var _dn = 0, _nc = 0, _dk;\n"
"      for (_dk in P) {\n"
"        if (_dk === 'constructor') continue;\n"
"        (function(_nm){\n"
"          var _f = P[_nm];\n"
"          if (typeof _f !== 'function') return;\n"
"          P[_nm] = function(){\n"
"            try { return _f.apply(this, arguments); }\n"
"            catch(_de){\n"
"              if (_dn < 30) {\n"
"                _dn++;\n"
"                try { print('[DOMERR] ' + _nm + '() :: ' + String(_de && _de.message ? _de.message : _de).slice(0, 160)); } catch(_dp){}\n"
"              }\n"
"              throw _de;\n"
"            }\n"
"          };\n"
"          _nc++;\n"
"        })(_dk);\n"
"      }\n"
"      P.__nfuDomArmed = 1;\n"
"      try { print('[DOMARMED] wrapped=' + _nc + ' errs=' + _dn); } catch(_da){}\n"
"    }\n"
"  } catch(_df){}\n"
"  // ---- typed array fast path ------------------------------------------\n"
"  // Draco's decode wrapper copies slices of the wasm heap with\n"
"  // TypedArray.prototype.copyWithin. QuickJS ships no builtin and the site's\n"
"  // polyfill is a per-element interpreted loop, which kept one eval busy for\n"
"  // minutes while the join chain sat idle; the engine exports a memmove\n"
"  // backed version instead.\n"
"  if (typeof g.__nfuTACopyWithin === 'function'){\n"
"    try {\n"
"      var _taNames = ['Int8Array','Uint8Array','Uint8ClampedArray','Int16Array','Uint16Array','Int32Array','Uint32Array','Float32Array','Float64Array'];\n"
"      for (var _ti = 0; _ti < _taNames.length; _ti++){\n"
"        var _TA = g[_taNames[_ti]];\n"
"        if (!_TA || !_TA.prototype) continue;\n"
"        _TA.prototype.copyWithin = function(t, s, e){ return g.__nfuTACopyWithin(this, t, s, e); };\n"
"      }\n"
"      try { print('[TAFAST] copyWithin native'); } catch(_tp){}\n"
"    } catch(_tae){}\n"
"  }\n"
"  // ---- timer / frame scheduler ------------------------------------------\n"
"  // Timers carry an id and a due time so mini_js_tick() can run only what is\n"
"  // ready. rAF lives in its own queue (__nfuRaf) and has NO frame cap: a game\n"
"  // loop must be able to schedule itself forever.\n"
"  var _tid = 1, _timers = [], _ivs = [];\n"
"  g.__nfuTimers = _timers;      // per-frame callback queue (mini_js_tick drains)\n""  // Worker traffic used its own queue: main<->worker messages pushed onto the\n"
"  // shared timer list were never drained (the queue stayed at a constant length\n"
"  // while the game kept posting geometry requests), so no worker ever answered\n"
"  // and the world mesh was never built. A private queue drained every pump keeps\n"
"  // worker delivery independent of how the timer list is rotated.\n"
"  g.__nfuWq = [];\n"
"  g.__nfuWDrain = function(limit){\n"
"    var q = g.__nfuWq; if (!q || !q.length) return 0;\n"
"    var n = 0, i;\n"
"    for (i = 0; i < q.length && n < limit; i++){\n"
"      var f = q[i];\n"
"      if (typeof f === 'function'){ n++; try { f(); } catch(e){ try { print('[WQ-ERR] ' + (e && e.message ? e.message : e)); } catch(pe){} } }\n"
"    }\n"
"    if (i > 0) q.splice(0, i);\n"
"    if (q.length > 4096) q.length = 4096;\n"
"    return n;\n"
"  };\n"
"  g.__nfuIntervals = _ivs;      // setInterval slots (re-armed every 'every' ms)\n"
"  g.__nfuNow = function(){ return (g.performance && g.performance.now) ? g.performance.now() : Date.now(); };\n"
"  // Extra arguments after the timeout are forwarded to the callback. Real browsers do this, and bloxd's tick driver relies on it: setTimeout(bR, 0, driver, true) - dropping the tail called bR(undefined) and every frame threw.\n"
"  g.setTimeout = function(f, ms){ if (typeof f !== 'function') return 0; var q = g.__nfuTimers; var id = _tid++; var a = Array.prototype.slice.call(arguments, 2); q.push({ id: id, f: f, a: a, due: g.__nfuNow() + (ms || 0) }); return id; };\n"
"  g.clearTimeout = function(id){ var q = g.__nfuTimers || []; for (var i = 0; i < q.length; i++) if (q[i].id === id) { q.splice(i, 1); return; } };\n"
"  // Same call-argument forwarding as setTimeout.\n"
"  g.setInterval = function(f, ms){ if (typeof f !== 'function') return 0; var id = _tid++; var a = Array.prototype.slice.call(arguments, 2); g.__nfuIntervals.push({ id: id, f: f, a: a, every: (ms || 16), due: g.__nfuNow() + (ms || 16) }); return id; };\n"
"  g.clearInterval = function(id){ var q = g.__nfuIntervals || []; for (var i = 0; i < q.length; i++) if (q[i].id === id) { q.splice(i, 1); return; } };\n"
"  var _rafId = 1;\n"
"  // rAF census: a game whose render loop never fires looks exactly like a page\n"
"  // that draws nothing, so count every registration and the queue depth.\n"
"  g.requestAnimationFrame = function(f){ if (typeof f !== 'function') return 0; var id = _rafId++; g.__nfuRafN = (g.__nfuRafN || 0) + 1; if (g.__nfuRafN <= 4 || g.__nfuRafN % 120 === 0) try { print('[RAFQ] n=' + g.__nfuRafN + ' q=' + ((g.__nfuRaf||[]).length) + ' src=' + String(f).slice(0, 70).replace(/\\s+/g, ' ')); } catch(e){} (g.__nfuRaf = g.__nfuRaf || []).push({ id: id, f: f }); return id; };\n"
"  g.cancelAnimationFrame = function(id){ var q = g.__nfuRaf; if (!q) return; for (var i=0;i<q.length;i++) if (q[i].id === id) { q.splice(i,1); return; } };\n"
"  g.requestIdleCallback = function(f){ return g.setTimeout(function(){ try { f({ timeRemaining: function(){ return 8; }, didTimeout: false }); } catch(e){} }, 1); };\n"
"  g.cancelIdleCallback = function(id){ g.clearTimeout(id); };\n"
"  // ---- event router ------------------------------------------------------\n"
"  // The browser hit-tests its rendered DOM and posts events back here with the\n"
"  // data-nfu index of the element under the pointer. Listeners on the element\n"
"  // and all ancestors (bubbling), then document, then window, then on<type>.\n"
"  g.__nfuWinEvents = g.__nfuWinEvents || {};\n"
"  g.addEventListener = function(t, f){ (g.__nfuWinEvents[t] = g.__nfuWinEvents[t] || []).push(f); };\n"
"  g.removeEventListener = function(t, f){ var a = g.__nfuWinEvents[t]; if (a){ var i = a.indexOf(f); if (i >= 0) a.splice(i,1); } };\n"
"  g.dispatchEvent = function(ev){ if (!ev) return true; g.__nfuFire(ev, null); return !ev.defaultPrevented; };\n"
"  g.__nfuFire = function(ev, tgt){\n"
"    var chain = [], n = tgt;\n"
"    while (n){ chain.push(n); n = n._parent; }\n"
"    if (g.document) chain.push(g.document);\n"
"    var t, node, a, i, j;\n"
"    for (i = 0; i < chain.length; i++){\n"
"      node = chain[i];\n"
"      if (!node) continue;\n"
"      // DOM 0 attribute handler: el.onchange = fn assigns a plain property\n"
"      // on the shim element. It must fire even when no addEventListener\n"
"      // listener exists, so it runs BEFORE the _events lookup below (which\n"
"      // 'continue's past nodes without listeners).\n"
"      if (!ev._stop){ var oh0 = node['on' + ev.type]; if (typeof oh0 === 'function'){ try { oh0.call(node, ev); } catch(e){ try { print('[MARK][EVTERR] ' + (e && e.message ? e.message : e)); } catch(x){} } } }\n"
"      if (!node || !node._events) continue;\n"
"      a = node._events[ev.type]; if (!a || !a.length) continue;\n"
"      ev.currentTarget = node;\n"
"      if (ev.type === 'click') { try { var pk = Object.keys(node).filter(function(k){ return k.indexOf('__reactProps$') === 0; }); var has = pk.length && node[pk[0]]; print('[MARK][FIRE] id=' + ((node._attrs && node._attrs.id) || '') + ' props=' + (pk.length ? Object.keys(has).join(',').substring(0, 200) : 'none')); } catch(pe){ print('[MARK][FIREERR] ' + pe); } }\n"
"      for (j = 0; j < a.length; j++){ try { if (typeof a[j] === 'function') a[j].call(node, ev); else if (a[j] && a[j].handleEvent) a[j].handleEvent(ev); } catch(e){ try { print('[MARK][EVTERR] ' + (e && e.message ? e.message : e)); } catch(x){} } }\n"
"      if (ev._stop) break;\n"
"    }\n"
"    if (!ev._stop){ t = g.__nfuWinEvents[ev.type]; if (t) for (j = 0; j < t.length; j++){ ev.currentTarget = g; try { t[j].call(g, ev); } catch(e){ try { print('[MARK][EVTERR] ' + (e && e.message ? e.message : e)); } catch(x){} } } }\n"
"    if (!ev._stop && typeof g['on' + ev.type] === 'function'){ try { g['on' + ev.type](ev); } catch(e){} }\n"
"    return !ev.defaultPrevented;\n"
"  };\n"
"  g.__nfuEvent = function(type, idx, x, y, key, mods, btn){\n"
"    if (String(type) === 'click') { try { print('[MARK][EV] click idx=' + idx + ' live=' + (_all[idx] ? 1 : 0)); } catch(e){} }\n"
"    var tgt = _all[idx] || (g.document && g.document.body) || null;\n"
"    mods = mods || 0;\n"
"    var ev = { type: String(type), target: tgt, currentTarget: tgt, srcElement: tgt,\n"
"               clientX: x || 0, clientY: y || 0, pageX: x || 0, pageY: y || 0, x: x || 0, y: y || 0,\n"
"               offsetX: x || 0, offsetY: y || 0, movementX: g.__nfuMvX || 0, movementY: g.__nfuMvY || 0,\n"
"               key: '', code: '', keyCode: 0, which: 0, charCode: 0, repeat: false,\n"
"               shiftKey: !!(mods & 1), ctrlKey: !!(mods & 2), altKey: !!(mods & 4), metaKey: !!(mods & 8),\n"
"               button: btn || 0, buttons: (type === 'mouseup' ? 0 : 1),\n"
"               relatedTarget: null, deltaX: 0, deltaY: 0, deltaMode: 0, wheelDelta: 0,\n"
"               bubbles: true, cancelable: true, composed: true, defaultPrevented: false, _stop: false,\n"
"               timeStamp: Date.now(), isTrusted: true,\n"
"               preventDefault: function(){ this.defaultPrevented = true; },\n"
"               stopPropagation: function(){ this._stop = true; },\n"
"               stopImmediatePropagation: function(){ this._stop = true; } };\n"
"    if (typeof key === 'number'){ ev.keyCode = key; ev.which = key; ev.charCode = key; }\n"
"    else if (typeof key === 'string' && key.length){\n"
"      ev.key = key; ev.code = key;\n"
"      // Standard key codes. Deriving them from the first letter (as in\n"
"      // 'Escape' -> 'E' -> 69) gives a wrong value, and pages that compare\n"
"      // e.keyCode === 27 to detect the Escape key would never match.\n"
"      var kc = { 'Escape':27, 'Enter':13, 'Tab':9, 'Backspace':8, 'Delete':46,\n"
"                 ' ':32, 'ArrowLeft':37, 'ArrowUp':38, 'ArrowRight':39,\n"
"                 'ArrowDown':40, 'Home':36, 'End':35, 'PageUp':33, 'PageDown':34,\n"
"                 'Shift':16, 'Control':17, 'Alt':18, 'Meta':91 }[key];\n"
"      if (kc === undefined) kc = (key.length === 1) ? key.toUpperCase().charCodeAt(0) : 0;\n"
"      ev.keyCode = kc; ev.which = kc; ev.charCode = 0;\n"
"    }\n"
"    // React handler dispatch runs BEFORE __nfuFire: a page-level\n"
"    // listener that calls stopPropagation (React's own root listener\n"
"    // chokes on our synthetic event) must not swallow the onClick.\n"
"    if (ev.type === 'click' && tgt) {\n"
"      var rn = tgt, ractFired = 0;\n"
"      while (rn) {\n"
"        var rk = null;\n"
"        try { var rks = Object.keys(rn).filter(function(k){ return k.indexOf('__reactProps$') === 0; }); if (rks.length) rk = rn[rks[0]]; } catch(re){}\n"
"        if (rk && typeof rk.onClick === 'function') {\n"
"          try { print('[MARK][RACT] onClick on #' + (rn._nfuIdx != null ? rn._nfuIdx : -1)); } catch(rx){}\n"
"          try { rk.onClick(ev); ractFired = 1; } catch(re2){ try { print('[MARK][REACTERR] ' + (re2 && re2.message ? re2.message : re2)); } catch(rx){} }\n"
"          break;\n"
"        }\n"
"        rn = rn._parent;\n"
"      }\n"
"      if (!ractFired) { try { print('[MARK][RACT] no onClick ancestor for #' + idx); } catch(rx){} }\n"
"      // Diagnostic + fallback: the props-on-host-node walk can miss handlers\n"
"      // React keeps on composite components (the host node then carries only\n"
"      // className), so follow the fiber return chain and call the first\n"
"      // onClick found there. Only the first few clicks are traced.\n"
"      try {\n"
"        g.__nfuClickDbg = (g.__nfuClickDbg || 0) + 1;\n"
"        if (g.__nfuClickDbg <= 4) {\n"
"          var cx = tgt, cg = 0;\n"
"          while (cx && cg < 14) {\n"
"            var cks = [];\n"
"            try { cks = Object.keys(cx).filter(function(k){ return k.indexOf('__reactProps$') === 0; }); } catch(ce){}\n"
"            var cpr = cks.length ? cx[cks[0]] : null;\n"
"            print('[CHAIN] #' + (cx._nfuIdx != null ? cx._nfuIdx : -1) + ' ' + String(cx._tag || '?') + ' cls=' + String((cx._attrs && cx._attrs['class']) || '').substring(0, 36) + ' props=' + (cpr ? Object.keys(cpr).join('|').substring(0, 110) : 'none'));\n"
"            cx = cx._parent; cg++;\n"
"          }\n"
"          var fbk = [];\n"
"          try { fbk = Object.keys(tgt).filter(function(k){ return k.indexOf('__reactFiber$') === 0; }); } catch(fe){}\n"
"          if (fbk.length) {\n"
"            var fb = tgt[fbk[0]], fg = 0;\n"
"            while (fb && fg < 16) {\n"
"              var mp = fb.memoizedProps, nm = '';\n"
"              try { nm = (typeof fb.elementType === 'string') ? fb.elementType : ((fb.elementType && fb.elementType.name) || '?'); } catch(ne){}\n"
"              if (mp) print('[FIB] ' + nm + ' props=' + Object.keys(mp).join('|').substring(0, 150));\n"
"              if (mp && !ractFired && typeof mp.onClick === 'function') {\n"
"                try { print('[MARK][FACT] onClick via fiber ' + nm); mp.onClick(ev); ractFired = 1; } catch(fe2){ try { print('[MARK][REACTERR] fiber ' + (fe2 && fe2.message ? fe2.message : fe2)); } catch(z){} }\n"
"              }\n"
"              fb = fb.return; fg++;\n"
"            }\n"
"          }\n"
"        }\n"
"      } catch(de){ try { print('[CHAINERR] ' + de); } catch(z2){} }\n"
"    }\n"
"    g.__nfuFire(ev, tgt);\n"
"    // React 17 compat shortcut: the delegated dispatcher at the root needs\n"
"    // deeper nativeEvent internals than we currently provide, so call the\n"
"    // nearest React onClick handler directly along the parent chain. The\n"
"    // synthetic event doubles as the handler argument (it carries target,\n"
"    // preventDefault, stopPropagation, ...). Skipped when a listener called\n"
"    // stopPropagation. Double-invocation is prevented by React itself not\n"
"    // completing its own dispatch on shim events today.\n"
"    // Keyboard events need the same shortcut, but the lookup direction is\n"
"    // different: a key event is delivered on document.body, while React's\n"
"    // onKeyDown lives on a node INSIDE the React root container - walking up\n"
"    // from the body would never find it. So look for the React root (the\n"
"    // node carrying a __reactContainer$ marker) and search its subtree for\n"
"    // the nearest element that declares onKeyDown / onKeyUp.\n"
"    if ((ev.type === 'keydown' || ev.type === 'keyup') && !ev._stop) {\n"
"      var root = null;\n"
"      try {\n"
"        for (var rk2 in _all) {\n"
"          var el2 = _all[rk2];\n"
"          if (!el2) continue;\n"
"          var ks2 = Object.keys(el2);\n"
"          for (var k2 = 0; k2 < ks2.length; k2++) {\n"
"            if (ks2[k2].indexOf('__reactContainer$') === 0) { root = el2; break; }\n"
"          }\n"
"          if (root) break;\n"
"        }\n"
"      } catch(re3){}\n"
"      if (root) {\n"
"        var found = null;\n"
"        var stack = [root];\n"
"        var guard = 0;\n"
"        while (stack.length && guard < 20000 && !found) {\n"
"          guard++;\n"
"          var node = stack.pop();\n"
"          var pr = null;\n"
"          try { var pk2 = Object.keys(node).filter(function(k){ return k.indexOf('__reactProps$') === 0; }); if (pk2.length) pr = node[pk2[0]]; } catch(re4){}\n"
"          if (pr) {\n"
"            var h = (ev.type === 'keydown') ? pr.onKeyDown : pr.onKeyUp;\n"
"            if (typeof h === 'function') found = { node: node, fn: h };\n"
"          }\n"
"          var kids = node._children || [];\n"
"          for (var ci = kids.length - 1; ci >= 0; ci--) stack.push(kids[ci]);\n"
"        }\n"
"        if (found) {\n"
"          ev.target = found.node;\n"
"          try { found.fn(ev); } catch(re5){ try { print('[MARK][REACTERR] ' + (re5 && re5.message ? re5.message : re5)); } catch(rx){} }\n"
"        }\n"
"      }\n"
"    }\n"
"    // Input events: React's onChange is delegated from the root container, so\n"
"    // a plain dispatch never reaches it. Call the prop on the element itself\n"
"    // and walk up like a bubbling listener would, so controlled inputs (search\n"
"    // boxes above all) update their state from our keystrokes.\n"
"    if ((ev.type === 'input' || ev.type === 'change') && !ev._stop && tgt) {\n"
"      var in2 = tgt;\n"
"      while (in2) {\n"
"        var ip = null;\n"
"        try { var ik = Object.keys(in2).filter(function(k){ return k.indexOf('__reactProps$') === 0; }); if (ik.length) ip = in2[ik[0]]; } catch(ie){}\n"
"        if (ip) {\n"
"          var ih = (ev.type === 'input') ? (ip.onChange || ip.onInput) : ip.onChange;\n"
"          if (typeof ih === 'function') {\n"
"            try { ih(ev); } catch(ie2){ try { print('[MARK][REACTERR] ' + (ie2 && ie2.message ? ie2.message : ie2)); } catch(rx){} }\n"
"            break;\n"
"          }\n"
"        }\n"
"        in2 = in2._parent;\n"
"      }\n"
"    }\n"
"    // A text field takes focus when clicked, so subsequent keystrokes (which\n"
"    // the host routes back as __nfuSetInput) land on the right element.\n"
"    if (ev.type === 'click' && tgt && tgt._tag) {\n"
"      var tg = String(tgt._tag).toLowerCase();\n"
"      if (tg === 'input' || tg === 'textarea') { try { g.document.activeElement = tgt; } catch(fe){} }\n"
"    }\n"
"    return !ev.defaultPrevented;\n"
"  };\n"
"  // ---- form submission ---------------------------------------------------\n"
"  // The host computes the target URL (it owns the DOM the user typed into) but\n"
"  // lets the page veto the load: `submit` is dispatched first and the host's\n"
"  // navigation only happens when nobody called preventDefault(). That is what\n"
"  // keeps JS-driven search boxes (React onSubmit -> fetch) from being blown\n"
"  // away by a full page load.\n"
"  g.__nfuSubmitEl = function(el, url, postBody){\n"
"    var tgt = el || (g.document && g.document.body) || null;\n"
"    var ev = { type: 'submit', target: tgt, currentTarget: tgt, srcElement: tgt,\n"
"               bubbles: true, cancelable: true, composed: true,\n"
"               defaultPrevented: false, _stop: false, submitter: null,\n"
"               timeStamp: Date.now(), isTrusted: true,\n"
"               preventDefault: function(){ this.defaultPrevented = true; },\n"
"               stopPropagation: function(){ this._stop = true; },\n"
"               stopImmediatePropagation: function(){ this._stop = true; } };\n"
"    if (tgt) { try { g.__nfuFire(ev, tgt); } catch(e){ try { print('[SUBMITERR] ' + e); } catch(x){} } }\n"
"    // React's onSubmit is delegated from the root container, so the bubbling\n"
"    // dispatch above never reaches it; call the nearest prop directly.\n"
"    if (!ev.defaultPrevented && !ev._stop) {\n"
"      var rn = tgt;\n"
"      while (rn) {\n"
"        var rk = null;\n"
"        try { var ks = Object.keys(rn).filter(function(k){ return k.indexOf('__reactProps$') === 0; }); if (ks.length) rk = rn[ks[0]]; } catch(re){}\n"
"        if (rk && typeof rk.onSubmit === 'function') {\n"
"          try { rk.onSubmit(ev); } catch(re2){ try { print('[MARK][REACTERR] ' + re2); } catch(rx){} }\n"
"          break;\n"
"        }\n"
"        rn = rn._parent;\n"
"      }\n"
"    }\n"
"    if (ev.defaultPrevented) return 0;\n"
"    // postBody is the urlencoded field list: a form with method=\"post\" must\n"
"    // send it as the request body, never as a query string. The host parks the\n"
"    // request and replays it on the next frame (a load started from inside a\n"
"    // script would re-enter the loader).\n"
"    if (postBody != null && typeof g.__nfuGoPost === 'function') {\n"
"      g.__nfuGoPost(String(url || ''), String(postBody));\n"
"      return 1;\n"
"    }\n"
"    if (url) g.__nfuNav(String(url), 0);\n"
"    return 1;\n"
"  };\n"
"  g.__nfuSubmit = function(idx, url, body){\n"
"    return g.__nfuSubmitEl((idx >= 0) ? (_all[idx] || null) : null, url, body);\n"
"  };\n"
"  // Static (server-rendered) pages have no data-nfu index in the native DOM, so\n"
"  // the host addresses those elements by id instead.\n"
"  g.__nfuSubmitById = function(id, url, body){\n"
"    var el = null;\n"
"    try { el = g.document.getElementById(String(id)); } catch(e){}\n"
"    return g.__nfuSubmitEl(el, url, body);\n"
"  };\n"
"  // ---- programmatic submission (form.submit / requestSubmit) -------------\n"
"  // Older sites validate first and then call form.submit(): no event, just a\n"
"  // request. The live control values only exist in this tree (React keeps them\n"
"  // in properties, not attributes), so serialize them here the way a browser\n"
"  // does and hand the finished URL/body to the host.\n"
"  function nfuFieldVal(n){\n"
"    if (n._val !== undefined && n._val !== null) return String(n._val);\n"
"    if (n._tag === 'textarea') return n._inner || '';\n"
"    var a = n._attrs['value'];\n"
"    return (a === undefined || a === null) ? '' : String(a);\n"
"  }\n"
"  function nfuFormBody(f){\n"
"    var out = [];\n"
"    function walk(n){\n"
"      if (!n || !n._tag) return;\n"
"      var t = n._tag, nm = n._attrs['name'], i;\n"
"      if (t === 'input' && nm){\n"
"        var ty = String(n._attrs['type'] || 'text').toLowerCase();\n"
"        if (ty === 'checkbox' || ty === 'radio'){\n"
"          var cv = (n._val !== undefined && n._val !== null) ? n._val : n._attrs['value'];\n"
"          if (n._checked) out.push(encodeURIComponent(nm) + '=' + encodeURIComponent(cv === undefined ? 'on' : cv));\n"
"        } else if (ty !== 'submit' && ty !== 'button' && ty !== 'reset' &&\n"
"                   ty !== 'image' && ty !== 'file'){\n"
"          out.push(encodeURIComponent(nm) + '=' + encodeURIComponent(nfuFieldVal(n)));\n"
"        }\n"
"      } else if (t === 'textarea' && nm){\n"
"        out.push(encodeURIComponent(nm) + '=' + encodeURIComponent(nfuFieldVal(n)));\n"
"      } else if (t === 'select' && nm){\n"
"        var opt = null, first = null;\n"
"        for (i = 0; i < n._children.length; i++){\n"
"          var ch = n._children[i];\n"
"          if (!ch || ch._tag !== 'option') continue;\n"
"          if (!first) first = ch;\n"
"          if (ch._checked || ch._attrs['selected'] !== undefined) { opt = ch; break; }\n"
"        }\n"
"        if (!opt) opt = first;\n"
"        if (opt) out.push(encodeURIComponent(nm) + '=' + encodeURIComponent(nfuFieldVal(opt) || textOf(opt)));\n"
"      }\n"
"      for (var k = 0; k < n._children.length; k++) walk(n._children[k]);\n"
"    }\n"
"    walk(f);\n"
"    return out.join('&');\n"
"  }\n"
"  P.submit = function(){\n"
"    if (!this || this._tag !== 'form') return;\n"
"    var act = this._attrs['action'] || (g.location && g.location.href) || '';\n"
"    var post = String(this._attrs['method'] || 'get').toLowerCase() === 'post';\n"
"    var body = nfuFormBody(this);\n"
"    if (post){\n"
"      if (typeof g.__nfuGoPost === 'function') g.__nfuGoPost(act, body);\n"
"      else if (act) g.__nfuNav(act, 0);\n"
"    } else if (act){\n"
"      g.__nfuNav(act + (body ? (act.indexOf('?') >= 0 ? '&' : '?') + body : ''), 0);\n"
"    }\n"
"  };\n"
"  P.requestSubmit = function(){\n"
"    if (!this || this._tag !== 'form') return;\n"
"    // Validation + the submit event come first in a real browser; the host\n"
"    // owns the field list, so let it run the event and decide.\n"
"    var act = this._attrs['action'] || (g.location && g.location.href) || '';\n"
"    if (typeof g.__nfuSubmitEl === 'function') g.__nfuSubmitEl(this, act);\n"
"  };\n"
"  g.__nfuClickById = function(id, x, y){\n"
"    var el = null;\n"
"    try { el = g.document.getElementById(String(id)); } catch(e){}\n"
"    if (!el) return 0;\n"
"    try { g.__nfuEvent('click', el._nfuIdx, x || 0, y || 0, '', 0, 0); } catch(e){ return 0; }\n"
"    return 1;\n"
"  };\n"
"  g.__nfuDispatch = function(idx, x, y){ try { try { var fk = Object.keys(_all[idx]).filter(function(k){ return k.indexOf('__reactFiber$') === 0; }); var propsKey = (Object.keys(_all[idx]).filter(function(k){ return k.indexOf('__reactProps$') === 0; })[0]) || ''; var node2 = _all[idx]; var pk2 = propsKey ? node2[propsKey] : null; print('[FIBER] fiber=' + fk.length + ' props=' + (pk2 ? Object.keys(pk2).join(',').substring(0,120) : 'none') + ' onClick=' + (pk2 && typeof pk2.onClick === 'function' ? 'YES' : 'no')); } catch(e2){ print('[FIBERERR] ' + e2); } var t = _all && _all[idx]; } catch(e){ try { print('[CHAINERR] ' + e); } catch(x){} } return g.__nfuEvent('click', idx, x, y, '', 0, 0); };\n"
"  // Click an element by its visible text. A data-nfu index is only stable\n"
"  // for one particular render, so a driver that wants to press \"Agree\" or\n"
"  // \"Play\" has to find the element by its label instead.\n"
"  g.__nfuClickText = function(txt){\n"
"    var want = String(txt || '').toLowerCase();\n"
"    if (!want) return -1;\n"
"    var best = -1;\n"
"    try {\n"
"      for (var k in _all) {\n"
"        var el = _all[k];\n"
"        if (!el || !el.textContent) continue;\n"
"        var t = String(el.textContent).replace(/\\s+/g, ' ').trim().toLowerCase();\n"
"        if (!t) continue;\n"
"        if (t === want) { best = Number(k); break; }\n"
"        if (best < 0 && t.indexOf(want) >= 0 && t.length < 80) best = Number(k);\n"
"      }\n"
"    } catch(e) { print('[MARK][CLICKTXT] ' + e); }\n"
"    if (best < 0) { print('[MARK][CLICKTXT] not found: ' + txt); return -1; }\n"
"    // The label usually sits in an inner node while the click handler lives\n"
"    // on an ancestor (a button's text is in a <div class=ButtonBody>, the\n"
"    // handler is on the wrapping <div class=NewButton>). Clicking the text\n"
"    // node does nothing, so walk up to the nearest ancestor that actually\n"
"    // carries a React onClick - exactly what a real click bubbles to.\n"
"    var target = _all[best];\n"
"    var anc = target;\n"
"    var clicked = -1;\n"
"    while (anc) {\n"
"      var rk = null;\n"
"      try { var rks = Object.keys(anc).filter(function(k){ return k.indexOf('__reactProps$') === 0; }); if (rks.length) rk = anc[rks[0]]; } catch(re){}\n"
"      if (rk && typeof rk.onClick === 'function') { clicked = anc._nfuIdx; break; }\n"
"      anc = anc._parent;\n"
"    }\n"
"    if (clicked < 0) clicked = best;\n"
"    print('[MARK][CLICKTXT] idx=' + clicked + ' text=' + txt);\n"
"    g.__nfuEvent('click', clicked, 0, 0, '', 0, 0);\n"
"    // The driver needs to know that the click actually went through: a\n"
"    // label that is not on the page yet must be retried, one that was\n"
"    // clicked must move on to the next step.\n"
"    g.__nfuClickHits = (g.__nfuClickHits || 0) + 1;\n"
"    return clicked;\n"
"  };\n"
"  // Click an element by a CSS class fragment. Matching on the visible text\n"
"  // alone is not enough: a button label often lives in an inner node while\n"
"  // the click handler sits on an outer wrapper, so clicking the text node\n"
"  // does nothing. (bloxd.io's consent dialog renders the text \"Agree\" in\n"
"  // <div class=\"ButtonBody\">Agree</div> while the clickable control is the\n"
"  // parent <div class=\"... PromptGenericPopupBodyButton\" role=\"button\">.)\n"
"  // The class fragment therefore identifies the control that actually reacts.\n"
"  g.__nfuClickClass = function(frag){\n"
"    var want = String(frag || '');\n"
"    if (!want) return -1;\n"
"    var best = -1;\n"
"    try {\n"
"      for (var k in _all) {\n"
"        var el = _all[k];\n"
"        if (!el) continue;\n"
"        var cn = el._attrs && el._attrs['class'];\n"
"        if (!cn) continue;\n"
"        if (String(cn).indexOf(want) >= 0) { best = Number(k); break; }\n"
"      }\n"
"    } catch(e) { print('[MARK][CLICKCLS] ' + e); }\n"
"    if (best < 0) { print('[MARK][CLICKCLS] not found: ' + frag); return -1; }\n"
"    print('[MARK][CLICKCLS] idx=' + best + ' class=' + frag);\n"
"    g.__nfuEvent('click', best, 0, 0, '', 0, 0);\n"
"    g.__nfuClickHits = (g.__nfuClickHits || 0) + 1;\n"
"    return best;\n"
"  };\n"
"  // ---- scripted page actions ---------------------------------------------\n"
"  // A driver (the terminal, the shot harness) describes what the page has\n"
"  // to do as a list of actions, and the page performs them itself:\n"
"  //   \"key:Escape\"  -> press a key\n"
"  //   \"text:Play\"   -> click the element showing that text\n"
"  //   \"class:Foo\"   -> click the element whose class contains \"Foo\"\n"
"  //\n"
"  // The page keeps the cursor and the retry count, because only the page\n"
"  // knows whether an action hit. A click that misses is retried: a dialog\n"
"  // often renders only after the dialog in front of it was dismissed, so a\n"
"  // one-shot attempt would skip it forever.\n"
"  g.__nfuActList = null;\n"
"  g.__nfuActIdx = 0;\n"
"  g.__nfuActTries = 0;\n"
"  g.__nfuActHit = 0;\n"
"  g.__nfuActDone = 0;\n"
"  // Execute ONE step of the action sequence and report whether it hit.\n"
"  //\n"
"  // The host calls this once per tick, so the retry rhythm is driven by the\n"
"  // host's clock. That matters: mini_js_run flushes every queued timer after\n"
"  // each eval, so a retry scheduled with setTimeout would burn through all\n"
"  // its attempts inside a single call and the sequence would give up long\n"
"  // before the page had rendered the control.\n"
"  //\n"
"  // The page keeps the cursor because only the page knows whether an action\n"
"  // hit. A click that misses is retried: a dialog often renders only after\n"
"  // the dialog in front of it was dismissed, so a one-shot attempt would\n"
"  // skip it forever.\n"
"  g.__nfuActRun = function(list){\n"
"    if (!list || !list.length) return -1;\n"
"    // A new list replaces the old one; the same list resumes where it was.\n"
"    if (g.__nfuActList !== list) {\n"
"      var same = false;\n"
"      if (g.__nfuActList && g.__nfuActList.length === list.length) {\n"
"        same = true;\n"
"        for (var q = 0; q < list.length; q++) if (g.__nfuActList[q] !== list[q]) { same = false; break; }\n"
"      }\n"
"      if (!same) { g.__nfuActList = list; g.__nfuActIdx = 0; g.__nfuActTries = 0; g.__nfuActDone = 0; }\n"
"    }\n"
"    // Sequence finished: the host keeps posting the same list every 1.5 s,\n"
"    // so bail out before the (expensive) probe serializes the whole tree.\n"
"    if (g.__nfuActDone) return -1;\n"
"    try { var b2 = (document.body && document.body.innerHTML) || ''; "
"      print('[MARK][ACTPROBE] step=' + g.__nfuActIdx + ' popup=' + (b2.indexOf('PrivacyPopup') >= 0 ? 1 : 0) + ' blen=' + b2.length "
"        + ' home=' + (b2.indexOf('HomePage') >= 0 ? 1 : 0) + ' hidden=' + (b2.indexOf('Hidden') >= 0 ? 1 : 0) "
"        + ' none=' + (b2.indexOf('display:none') >= 0 ? 1 : 0) + ' head=' + b2.substring(0, 110)); } catch(e){}\n"
"    if (g.__nfuActIdx >= g.__nfuActList.length) { g.__nfuActDone = 1; return g.__nfuActIdx; }\n"
"    var act = String(g.__nfuActList[g.__nfuActIdx] || '');\n"
"    var kind = 'text', val = act;\n"
"    if (act.indexOf('key:') === 0) { kind = 'key'; val = act.substring(4); }\n"
"    else if (act.indexOf('text:') === 0) { kind = 'text'; val = act.substring(5); }\n"
"    else if (act.indexOf('class:') === 0) { kind = 'class'; val = act.substring(6); }\n"
"    var hit = 0;\n"
"    try {\n"
"      if (kind === 'key') { g.__nfuKey(val); hit = 1; }\n"
"      else if (kind === 'class') { hit = g.__nfuClickClass(val) >= 0 ? 1 : 0; }\n"
"      else { hit = g.__nfuClickText(val) >= 0 ? 1 : 0; }\n"
"    } catch(e) { print('[MARK][ACT] ' + e); hit = 0; }\n"
"    g.__nfuActHit = hit;\n"
"    if (hit) {\n"
"      print('[MARK][ACT] ' + (g.__nfuActIdx + 1) + '/' + g.__nfuActList.length + ' done: ' + act);\n"
"      g.__nfuActIdx++; g.__nfuActTries = 0;\n"
"    } else {\n"
"      g.__nfuActTries++;\n"
"      // Give up on an action that keeps missing (the control does not exist\n"
"      // on this page) so the sequence can continue.\n"
"      if (g.__nfuActTries > 40) {\n"
"        print('[ACT] gave up on: ' + act);\n"
"        g.__nfuActIdx++; g.__nfuActTries = 0;\n"
"      }\n"
"    }\n"
"    if (g.__nfuActIdx >= g.__nfuActList.length) { g.__nfuActDone = 1; print('[MARK][ACTDONE]'); }\n"
"    return g.__nfuActIdx;\n"
"  };\n"
"  // Press a key at the page. The event is delivered on document.body and\n"
"  // bubbles up to documentElement, the document and the window, which is\n"
"  // where pages listen for it (the floating-ui dismiss handler sits on\n"
"  // document.documentElement).\n"
"  g.__nfuKey = function(name){\n"
"    var n = String(name || '');\n"
"    if (!n) return 0;\n"
"    var key = n, code = n;\n"
"    var aliases = { esc:'Escape', escape:'Escape', enter:'Enter', return:'Enter',\n"
"                     tab:'Tab', space:' ', del:'Delete', delete:'Delete',\n"
"                     backspace:'Backspace', up:'ArrowUp', down:'ArrowDown',\n"
"                     left:'ArrowLeft', right:'ArrowRight' };\n"
"    var low = n.toLowerCase();\n"
"    if (aliases[low]) key = aliases[low];\n"
"    code = (key === ' ') ? 'Space' : key;\n"
"    g.__nfuEvent('keydown', -1, 0, 0, key, 0, 0);\n"
"    g.__nfuEvent('keyup', -1, 0, 0, key, 0, 0);\n"
"    print('[KEY] ' + key);\n"
"    return 1;\n"
"  };\n"
"  // Host -> page bridge for natively edited text fields: the value lives in\n"
"  // the C++ DOM (that is what gets painted), and every edit is replayed here\n"
"  // as value + an 'input' event so page handlers stay in sync.\n"
"  g.__nfuSetInput = function(idx, val){\n"
"    var el = _all[idx];\n"
"    if (!el) return 0;\n"
"    try { el.value = String(val == null ? '' : val); } catch(e){}\n"
"    g.__nfuEvent('input', idx, 0, 0, '', 0, 0);\n"
"    return 1;\n"
"  };\n"
"  // Host -> page: the user ticked a checkbox / picked a radio. The host owns\n"
"  // the painted state, so this only mirrors it and raises `change` (React\n"
"  // wires onChange to exactly that for boxes; __nfuEvent has the direct\n"
"  // __reactProps$.onChange shortcut for elements React is driving).\n"
"  g.__nfuSetChecked = function(idx, v){\n"
"    var el = _all[idx];\n"
"    if (!el) return 0;\n"
"    try { el.checked = !!v; } catch(e){}\n"
"    g.__nfuEvent('change', idx, 0, 0, '', 0, 0);\n"
"    return 1;\n"
"  };\n"
"  // Host -> page: the closed <select> was cycled to option N.\n"
"  g.__nfuSelect = function(idx, optIdx){\n"
"    var el = _all[idx];\n"
"    if (!el || !el._children) return 0;\n"
"    var opts = [], i, c;\n"
"    for (i = 0; i < el._children.length; i++){\n"
"      c = el._children[i];\n"
"      if (c && String(c._tag).toLowerCase() === 'option') opts.push(c);\n"
"    }\n"
"    if (!opts.length) return 0;\n"
"    var val = '';\n"
"    for (i = 0; i < opts.length; i++){\n"
"      var on = (i === optIdx);\n"
"      try { opts[i].checked = on; } catch(e){}\n"
"      if (on){\n"
"        if (opts[i]._val !== undefined) val = opts[i]._val;\n"
"        else if (opts[i]._attrs['value'] !== undefined) val = opts[i]._attrs['value'];\n"
"        else val = opts[i].textContent || '';\n"
"      }\n"
"    }\n"
"    try { el.value = String(val); } catch(e){}\n"
"    g.__nfuEvent('change', idx, 0, 0, '', 0, 0);\n"
"    return 1;\n"
"  };\n"
"  g.__nfuFocusEl = function(idx, on){\n"
"    var el = _all[idx];\n"
"    if (!el) return 0;\n"
"    try {\n"
"      if (on) g.document.activeElement = el;\n"
"      else if (g.document.activeElement === el) g.document.activeElement = g.document.body;\n"
"    } catch(e){}\n"
"    return 1;\n"
"  };\n"
"  // ---- nefuOS global stubs for framework init ----\n"
"  var _lsData = {};\n"
"  var _mkStore = function(){\n"
"    var store = {};\n"
"    return { getItem: function(k){ return (k in store) ? store[k] : null; }, setItem: function(k, v){ store[k] = String(v); }, removeItem: function(k){ delete store[k]; }, clear: function(){ store = {}; }, key: function(i){ var ks = Object.keys(store); return ks[i] || null; }, get length(){ return Object.keys(store).length; } };\n"
"  };\n"
"  g.localStorage = _mkStore();\n"
"  g.sessionStorage = _mkStore();\n"
"  // Traffic-code gate: bloxd only calls the Turnstile widget when its\n"
"  // \"refresh needed\" marker is expired or missing. When a signed code is\n"
"  // available locally the stamp is pre-seeded, so the page skips the widget\n"
"  // entirely and goes straight to the code request the native layer answers.\n"
"  try {\n"
"    if (typeof __nfuTrafficSkip === 'function' && __nfuTrafficSkip()){\n"
"      g.localStorage.setItem('bloxd-trafficCode_b_until', String(Date.now() + 604800000));\n"
"    }\n"
"  } catch(e){}\n"
"  // Emulated anonymous identity for bloxd.\n"
"  //\n"
"  // The field set mirrors what the real login endpoint returns and what the\n"
"  // app destructures: anonymizedAccountInfo / name / cosmetics / ranks /\n"
"  // isEduTeacher / availableUserContextTypes / \"3PSIDMCPP\" / whamm / ...\n"
"  //\n"
"  // \"whamm\" is the account handle used for the social API and to derive the\n"
"  // Colyseus room name, so it must exist and stay stable across reloads -\n"
"  // it is therefore cached in localStorage.\n"
"  g.__nfuIdentityJson = function(){\n"
"    var key = 'nfu-bloxd-identity', id = null;\n"
"    try { id = g.localStorage && g.localStorage.getItem(key); } catch(e){}\n"
"    if (!id) {\n"
"      id = 'nfu-' + Date.now().toString(36) + '-' + Math.floor(Math.random() * 0x1000000).toString(36);\n"
"      try { if (g.localStorage) g.localStorage.setItem(key, id); } catch(e){}\n"
"    }\n"
"    return JSON.stringify({\n"
"      anonymizedAccountInfo: { id: id },\n"
"      name: 'Player', username: 'Player', email: '', avatarUrl: '',\n"
"      whamm: id, token: 'nfu-anon-token', _id: id,\n"
"      cosmetics: {}, ranks: [], guilds: [],\n"
"      isEduTeacher: false, availableUserContextTypes: ['ANONYMOUS'],\n"
"      '3PSIDMCPP': false, requiresTrafficCode: false, countryCode: '',\n"
"      superRankFrom: 0, superRankForDays: 0, superRankHistory: [],\n"
"      showSuperRankWelcome: false, ownedCosmeticPacks: [], superGems: 0,\n"
"      personalWorldLobbyName: '', recentlyPlayedGames: [], error: null\n"
"    });\n"
"  };\n"
"  // The bundle stamps its compliance token as `..._secure=` + window.isSecureContext.\n"
"  // Undefined there put the word undefined into the join options, so report the\n"
"  // values a real https page reports instead.\n"
"  if (g.isSecureContext === undefined) g.isSecureContext = true;\n"
"  if (g.crossOriginIsolated === undefined) g.crossOriginIsolated = false;\n"
"  if (g.originAgentCluster === undefined) g.originAgentCluster = true;\n"
"  g.navigator = g.navigator || { userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36', platform: 'Win32', language: 'en-US', languages: ['en-US'], onLine: true, hardwareConcurrency: 4, deviceMemory: 8, maxTouchPoints: 0, cookieEnabled: true, vendor: 'Google Inc.', appVersion: '5.0 (Windows)', sendBeacon: function(){ return true; }, getGamepads: function(){ return []; } };\n"
"  g.location = g.location || { __href: '', protocol: 'https:', host: 'bloxd.io', hostname: 'bloxd.io', pathname: '/', search: '', hash: '', origin: 'https://bloxd.io' };\n"
"  // ---- pixel-backed 2D canvas ------------------------------------------\n"
"  // bloxd builds its block atlas in 2D canvas space: decodeImage() paints the\n"
"  // PNG, rotate()+drawImage() bakes the rotated tile variants, getImageData()\n"
"  // reads the bytes back. A no-op 2D context silently broke that pipeline, so\n"
"  // this context owns a real RGBA backing store keyed to canvas.width/height.\n"
"  var _cvPix = function(cv){\n"
"    var w = cv.width | 0, h = cv.height | 0;\n"
"    if (w < 0) w = 0; if (h < 0) h = 0;\n"
"    // A bogus canvas size (usually a runaway CSS-derived box) must not turn into\n"
"    // a multi-GB typed array: cap the backing store and report the case once.\n"
"    if ((w * h) > 4194304){\n"
"      if (!cv.__nfuBig){ cv.__nfuBig = 1; try { print('[CVPIX-BIG] ' + w + 'x' + h); } catch(pe){} }\n"
"      w = 2048; h = 2048;\n"
"    }\n"
"    if (!cv._px || cv._pxW !== w || cv._pxH !== h){\n"
"      cv._px = new Uint8ClampedArray(w * h * 4); cv._pxW = w; cv._pxH = h;\n"
"    }\n"
"    return cv._px;\n"
"  };\n"
"  var _cssColor = function(s){\n"
"    if (typeof s !== 'string') return [0, 0, 0, 255];\n"
"    s = s.replace(/^[ \\t]+|[ \\t]+$/g, '');\n"
"    if (s.charAt(0) === '#'){\n"
"      if (s.length === 4) return [parseInt(s.charAt(1) + s.charAt(1), 16), parseInt(s.charAt(2) + s.charAt(2), 16), parseInt(s.charAt(3) + s.charAt(3), 16), 255];\n"
"      if (s.length >= 7) return [parseInt(s.slice(1, 3), 16), parseInt(s.slice(3, 5), 16), parseInt(s.slice(5, 7), 16), 255];\n"
"      return [0, 0, 0, 255];\n"
"    }\n"
"    if (s.indexOf('rgb') === 0){\n"
"      var m = s.replace(/[^0-9.,]/g, '').split(',');\n"
"      var a = (m.length > 3) ? Math.round(parseFloat(m[3]) * 255) : 255;\n"
"      return [parseInt(m[0], 10) | 0, parseInt(m[1], 10) | 0, parseInt(m[2], 10) | 0, a];\n"
"    }\n"
"    if (s === 'white') return [255, 255, 255, 255];\n"
"    if (s === 'black') return [0, 0, 0, 255];\n"
"    if (s === 'transparent') return [0, 0, 0, 0];\n"
"    return [0, 0, 0, 255];\n"
"  };\n"
"  var _mkCtx2d = function(){\n"
"    var cv = this, M = [1, 0, 0, 1, 0, 0], saved = [];\n"
"    var mx = function(x, y){ return M[0] * x + M[2] * y + M[4]; };\n"
"    var my = function(x, y){ return M[1] * x + M[3] * y + M[5]; };\n"
"    var ctx = { canvas: cv, fillStyle: '#000', strokeStyle: '#000', lineWidth: 1, font: '10px sans-serif', textAlign: 'left', textBaseline: 'alphabetic', globalAlpha: 1, globalCompositeOperation: 'source-over', shadowBlur: 0, shadowColor: 'transparent', shadowOffsetX: 0, shadowOffsetY: 0, lineCap: 'butt', lineJoin: 'miter', miterLimit: 10, imageSmoothingEnabled: true, imageSmoothingQuality: 'low', lineDashOffset: 0, direction: 'ltr' };\n"
"    var _sel = function(x, y, w, h, keep){\n"
"      var dwc = cv.width | 0, dhc = cv.height | 0;\n"
"      if (dwc <= 0 || dhc <= 0) return;\n"
"      var p1x = mx(x, y), p1y = my(x, y), p2x = mx(x + w, y), p2y = my(x + w, y), p3x = mx(x, y + h), p3y = my(x, y + h), p4x = mx(x + w, y + h), p4y = my(x + w, y + h);\n"
"      var x0 = Math.floor(Math.min(p1x, p2x, p3x, p4x)), x1 = Math.ceil(Math.max(p1x, p2x, p3x, p4x));\n"
"      var y0 = Math.floor(Math.min(p1y, p2y, p3y, p4y)), y1 = Math.ceil(Math.max(p1y, p2y, p3y, p4y));\n"
"      if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > dwc) x1 = dwc; if (y1 > dhc) y1 = dhc;\n"
"      var dst = _cvPix(cv), c = _cssColor(ctx.fillStyle), X, Y, i;\n"
"      for (Y = y0; Y < y1; Y++) for (X = x0; X < x1; X++){\n"
"        i = (Y * dwc + X) * 4;\n"
"        dst[i] = keep ? c[0] : 0; dst[i + 1] = keep ? c[1] : 0; dst[i + 2] = keep ? c[2] : 0; dst[i + 3] = keep ? c[3] : 0;\n"
"      }\n"
"    };\n"
"    ctx.fillRect = function(x, y, w, h){ _sel(x, y, w, h, true); };\n"
"    ctx.clearRect = function(x, y, w, h){ _sel(x, y, w, h, false); };\n"
"    ctx.strokeRect = function(){};\n"
"    ctx.fillText = function(){}; ctx.strokeText = function(){};\n"
"    ctx.measureText = function(t){ return { width: String(t).length * 7, actualBoundingBoxAscent: 8, actualBoundingBoxDescent: 2 }; };\n"
"    ctx.drawImage = function(src, a1, a2, a3, a4){\n"
"      var sw = 0, sh = 0, sp = null;\n"
"      if (src && src._tag === 'canvas'){ sw = src.width | 0; sh = src.height | 0; sp = _cvPix(src); }\n"
"      else if (src && src.width && src.data){ sw = src.width | 0; sh = src.height | 0; sp = src.data; }\n"
"      if (!sp || sw <= 0 || sh <= 0) return;\n"
"      var dwc = cv.width | 0, dhc = cv.height | 0;\n"
"      if (dwc <= 0 || dhc <= 0) return;\n"
"      var dx = a1 | 0, dy = a2 | 0, dw = sw, dh = sh;\n"
"      if (a3 !== undefined && a4 !== undefined){ dw = a3 | 0; dh = a4 | 0; }\n"
"      if (dw <= 0 || dh <= 0) return;\n"
"      var dst = _cvPix(cv), x, y, sx, sy, si, di, sa, X, Y, ca, ia, oa;\n"
"      for (y = 0; y < dh; y++){\n"
"        sy = Math.floor(y * sh / dh);\n"
"        if (sy < 0 || sy >= sh) continue;\n"
"        for (x = 0; x < dw; x++){\n"
"          sx = Math.floor(x * sw / dw);\n"
"          if (sx < 0 || sx >= sw) continue;\n"
"          si = (sy * sw + sx) * 4; sa = sp[si + 3];\n"
"          if (!sa) continue;\n"
"          X = Math.round(mx(dx + x, dy + y)); Y = Math.round(my(dx + x, dy + y));\n"
"          if (X < 0 || Y < 0 || X >= dwc || Y >= dhc) continue;\n"
"          di = (Y * dwc + X) * 4;\n"
"          if (sa === 255){ dst[di] = sp[si]; dst[di + 1] = sp[si + 1]; dst[di + 2] = sp[si + 2]; dst[di + 3] = 255; }\n"
"          else {\n"
"            ca = dst[di + 3]; ia = 255 - sa; oa = sa + ca * ia / 255; if (!oa) oa = 1;\n"
"            dst[di] = (sp[si] * sa + dst[di] * ca * ia / 255) / oa;\n"
"            dst[di + 1] = (sp[si + 1] * sa + dst[di + 1] * ca * ia / 255) / oa;\n"
"            dst[di + 2] = (sp[si + 2] * sa + dst[di + 2] * ca * ia / 255) / oa;\n"
"            dst[di + 3] = oa;\n"
"          }\n"
"        }\n"
"      }\n"
"    };\n"
"    ctx.getImageData = function(x, y, w, h){\n"
"      x = x | 0; y = y | 0; w = Math.max(0, w | 0); h = Math.max(0, h | 0);\n"
"      var out = new Uint8ClampedArray(w * h * 4), dwc = cv.width | 0, dhc = cv.height | 0, src = _cvPix(cv);\n"
"      var r, c2, si, di;\n"
"      for (r = 0; r < h; r++){\n"
"        var sy = y + r; if (sy < 0 || sy >= dhc) continue;\n"
"        for (c2 = 0; c2 < w; c2++){\n"
"          var sx = x + c2; if (sx < 0 || sx >= dwc) continue;\n"
"          si = (sy * dwc + sx) * 4; di = (r * w + c2) * 4;\n"
"          out[di] = src[si]; out[di + 1] = src[si + 1]; out[di + 2] = src[si + 2]; out[di + 3] = src[si + 3];\n"
"        }\n"
"      }\n"
"      return { data: out, width: w, height: h };\n"
"    };\n"
"    ctx.putImageData = function(img, x, y){\n"
"      if (!img || !img.data) return;\n"
"      var dwc = cv.width | 0, dhc = cv.height | 0, dst = _cvPix(cv), w = img.width | 0, h = img.height | 0, r, c2, si, di;\n"
"      for (r = 0; r < h; r++){\n"
"        var dy = (y | 0) + r; if (dy < 0 || dy >= dhc) continue;\n"
"        for (c2 = 0; c2 < w; c2++){\n"
"          var dx = (x | 0) + c2; if (dx < 0 || dx >= dwc) continue;\n"
"          di = (dy * dwc + dx) * 4; si = (r * w + c2) * 4;\n"
"          dst[di] = img.data[si]; dst[di + 1] = img.data[si + 1]; dst[di + 2] = img.data[si + 2]; dst[di + 3] = img.data[si + 3];\n"
"        }\n"
"      }\n"
"    };\n"
"    ctx.createImageData = function(w, h){\n"
"      if (w && typeof w === 'object'){ h = w.height; w = w.width; }\n"
"      w = Math.max(0, w | 0); h = Math.max(0, h | 0);\n"
"      return { data: new Uint8ClampedArray(w * h * 4), width: w, height: h };\n"
"    };\n"
"    ctx.save = function(){ saved.push(M.slice()); }; \n"
"    ctx.restore = function(){ if (saved.length) M = saved.pop(); };\n"
"    ctx.translate = function(tx, ty){ M[4] += M[0] * tx + M[2] * ty; M[5] += M[1] * tx + M[3] * ty; };\n"
"    ctx.rotate = function(a){\n"
"      var c = Math.cos(a), s = Math.sin(a);\n"
"      if (Math.abs(c) < 1e-9) c = 0; if (Math.abs(s) < 1e-9) s = 0; if (Math.abs(c) < 1e-9) c = 0;\n"
"      var a0 = M[0], b0 = M[1], c0 = M[2], d0 = M[3];\n"
"      M[0] = a0 * c + c0 * s; M[1] = b0 * c + d0 * s;\n"
"      M[2] = c0 * c - a0 * s; M[3] = d0 * c - b0 * s;\n"
"    };\n"
"    ctx.scale = function(sx, sy){ M[0] *= sx; M[1] *= sx; M[2] *= sy; M[3] *= sy; };\n"
"    ctx.transform = function(a, b, c, d, e, f){\n"
"      var a0 = M[0], b0 = M[1], c0 = M[2], d0 = M[3];\n"
"      M[0] = a0 * a + c0 * b; M[1] = b0 * a + d0 * b;\n"
"      M[2] = a0 * c + c0 * d; M[3] = b0 * c + d0 * d;\n"
"      M[4] += a0 * e + c0 * f; M[5] += b0 * e + d0 * f;\n"
"    };\n"
"    ctx.setTransform = function(a, b, c, d, e, f){ if (typeof a === 'object'){ M = [a.a, a.b, a.c, a.d, a.e, a.f]; } else M = [a, b, c, d, e, f]; };\n"
"    ctx.resetTransform = function(){ M = [1, 0, 0, 1, 0, 0]; };\n"
"    ctx.getTransform = function(){ return { a: M[0], b: M[1], c: M[2], d: M[3], e: M[4], f: M[5] }; };\n"
"    ctx.beginPath = function(){}; ctx.closePath = function(){}; ctx.moveTo = function(){};\n"
"    ctx.lineTo = function(){}; ctx.quadraticCurveTo = function(){}; ctx.bezierCurveTo = function(){};\n"
"    ctx.arc = function(){}; ctx.arcTo = function(){}; ctx.ellipse = function(){}; ctx.rect = function(){};\n"
"    ctx.fill = function(){}; ctx.stroke = function(){}; ctx.clip = function(){};\n"
"    ctx.createLinearGradient = function(){ return { addColorStop: function(){} }; };\n"
"    ctx.createRadialGradient = function(){ return { addColorStop: function(){} }; };\n"
"    ctx.createPattern = function(){ return {}; };\n"
"    ctx.setLineDash = function(){}; ctx.getLineDash = function(){ return []; };\n"
"    ctx.isPointInPath = function(){ return false; }; ctx.isPointInStroke = function(){ return false; };\n"
"    ctx.getContextAttributes = function(){ return {}; };\n"
"    return ctx;\n"
"  };\n"
"  P.getContext = function(type){\n"
"    if (type === '2d') { if (!this.__ctx2d) this.__ctx2d = _mkCtx2d.call(this); return this.__ctx2d; }\n"
"    // GLES 2.0 / WebGL1 presentation. The swgl interpreter is at its best\n"
"    // on GLSL ES 1.00 (attribute/varying/gl_FragColor) and the WebGL1 API\n"
"    // surface is the one the bridge fully covers. Advertising webgl2 pushed\n"
"    // Babylon onto the WebGL2 path (ES 3.00 + VAO + instancing), so\n"
"    // deliberately return null for 'webgl2' and let engines fall back to\n"
"    // their WebGL1 code path via 'webgl' / 'experimental-webgl'.\n"
"    if (type === 'webgl2') {\n"
"      // Fingerprinting code (the Turnstile challenge) probes webgl2 and then\n"
"      // dereferences the result -- returning null here was a real null-deref\n"
"      // source. Alias it onto the WebGL1 bridge regardless of who created the\n"
"      // canvas: engines that insist on ES 3.00 still degrade through it.\n"
"      if (g.__nfuDumpFrame){ try { print('[CTX-WEBGL2] alias'); } catch(pc1){} }\n"
"    }\n"
"    if ((type === 'webgl' || type === 'experimental-webgl' || type === 'webgl2') && g.__getGL) {\n"
"      // Games often create their canvas without an id (bloxd does). The bridge\n"
"      // keys surfaces by id, so mint one and mirror it as an attribute - the\n"
"      // re-parsed DOM then finds the same surface again on the next commit\n"
"      // instead of allocating a second, differently sized one.\n"
"      var cid = this._attrs['id'];\n"
"      if (!cid) { if (!this.__cid) this.__cid = 'nfu_cv' + (g.__cvSeq = (g.__cvSeq || 0) + 1); cid = this.__cid; this._attrs['id'] = cid; }\n"
"      // Hand the native bridge the size the page asked for: canvas.width is a\n"
"      // property, so the native DOM (built from serialized markup) still holds\n"
"      // the 300x150 default at this moment. Format: id|WxH.\n"
"      var gl = g.__getGL(cid + '@' + (this.width | 0) + 'x' + (this.height | 0));\n"
"      if (!gl) return null;\n"
"      // Real WebGL contexts expose the owning element and the drawing-buffer\n"
"      // size as properties. three.js reads gl.canvas.width during renderer\n"
"      // construction, so a missing .canvas made every engine die with\n"
"      // 'cannot read property width of undefined'.\n"
"      if (!gl.canvas) gl.canvas = this;\n"
"      if (gl.drawingBufferWidth === undefined) gl.drawingBufferWidth = (this.width | 0) || 300;\n"
"      if (gl.drawingBufferHeight === undefined) gl.drawingBufferHeight = (this.height | 0) || 150;\n"
"      // One wrapper per canvas element: engines may call getContext twice\n"
"      // and compare identity, so the wrapper is cached on the element.\n"
"      if (this.__glw) return this.__glw;\n"
"      if (gl && !gl.getExtension) {\n"
"        // Keep the bridge's numeric getParameter reachable under a private\n"
"        // name before the wrapper below shadows the public method.\n"
"        if (!gl.__nfuGetParameter && gl.getParameter) gl.__nfuGetParameter = gl.getParameter;\n"
"        // GPU-info probes (bloxd engine detection) need these even though\n"
"        // real rendering goes through the native GL bridge\n"
"        gl.getExtension = function(name){\n"
"          if (name === 'WEBGL_debug_renderer_info') return { UNMASKED_VENDOR_WEBGL: 37445, UNMASKED_RENDERER_WEBGL: 37446 };\n"
"          if (name === 'WEBGL_lose_context') return { loseContext: function(){}, restoreContext: function(){} };\n"
"          // Voxel meshes pass 65536 vertices per chunk, so the 32-bit index path\n"
"          // is not optional: without it an engine splits every chunk into many\n"
"          // 16-bit draws or refuses the mesh. The rasterizer decodes UNSIGNED_INT\n"
"          // elements already, and this extension requires no entry points, so the\n"
"          // empty object is an honest answer rather than a stub.\n"
"          if (name === 'OES_element_index_uint') return {};\n"
"          // OES_vertex_array_object and ANGLE_instanced_arrays deliberately stay\n"
"          // null: engines call their entry points the moment the object exists,\n"
"          // and the bridge has no VAO or instancing path, so advertising them\n"
"          // turns a clean fallback into a crash on the first draw.\n"
"          return null;\n"
"        };\n"
"        gl.getSupportedExtensions = function(){\n"
"          return ['WEBGL_debug_renderer_info', 'WEBGL_lose_context', 'OES_element_index_uint'];\n"
"        };\n"
"        gl.getParameter = function(p){\n"
"          if (p === 37445) return 'Google Inc. (NVIDIA)';\n"
"          if (p === 37446) return 'ANGLE (NVIDIA, NVIDIA GeForce GTX 1050 (0x00001C81) Direct3D11 vs_5_0 ps_5_0, D3D11)';\n"
"          if (p === 7936 || p === 7937) return 'WebKit';\n"
"          if (p === 7938) return 'WebGL GLSL ES 1.00';\n"
"          if (p === 7939) return 'WebGL 1.0 (OpenGL ES 2.0 Chromium)';\n"
"          // Everything else MUST go to the bridge: the earlier version returned\n"
"          // null for every numeric pname, which made Babylon read 0 for\n"
"          // maxTextureSize / vertexTextureImageUnits and quietly disable the\n"
"          // texture + terrain paths the game needs.\n"
"          if (gl.__nfuGetParameter && p !== undefined && gl.__nfuGetParameter !== gl.getParameter) return gl.__nfuGetParameter(p);\n"
"          return null;\n"
"        };\n"
"        gl.getShaderPrecisionFormat = function(){ return { rangeMin: 127, rangeMax: 127, precision: 23 }; };\n"
"      }\n"
"      // WebGL1 enum constants. The native bridge answers by numeric pname and\n"
"      // only exports the handful of enums it interprets, but every engine\n"
"      // dereferences gl.COMPILE_STATUS / gl.ARRAY_BUFFER / gl.TRIANGLES: an\n"
"      // undefined constant silently turns a successful link into a failure and\n"
"      // a buffer target into NaN. Fill in the whole GLES 2.0 table here, once\n"
"      // per context, without overwriting anything the bridge already set.\n"
"      var NFU_GLC = {\n"
"        DEPTH_BUFFER_BIT:256, STENCIL_BUFFER_BIT:1024, COLOR_BUFFER_BIT:16384,\n"
"        POINTS:0, LINES:1, LINE_LOOP:2, LINE_STRIP:3, TRIANGLES:4, TRIANGLE_STRIP:5, TRIANGLE_FAN:6,\n"
"        ZERO:0, ONE:1, NONE:0,\n"
"        SRC_COLOR:768, ONE_MINUS_SRC_COLOR:769, SRC_ALPHA:770, ONE_MINUS_SRC_ALPHA:771,\n"
"        DST_ALPHA:772, ONE_MINUS_DST_ALPHA:773, DST_COLOR:774, ONE_MINUS_DST_COLOR:775, SRC_ALPHA_SATURATE:776,\n"
"        CONSTANT_COLOR:32769, ONE_MINUS_CONSTANT_COLOR:32770, CONSTANT_ALPHA:32771, ONE_MINUS_CONSTANT_ALPHA:32772, BLEND_COLOR:32773,\n"
"        FUNC_ADD:32774, FUNC_SUBTRACT:32778, FUNC_REVERSE_SUBTRACT:32779,\n"
"        BLEND_EQUATION:32777, BLEND_EQUATION_RGB:32777, BLEND_EQUATION_ALPHA:34877,\n"
"        BLEND_DST_RGB:32968, BLEND_SRC_RGB:32969, BLEND_DST_ALPHA:32970, BLEND_SRC_ALPHA:32971,\n"
"        ARRAY_BUFFER:34962, ELEMENT_ARRAY_BUFFER:34963, ARRAY_BUFFER_BINDING:34964, ELEMENT_ARRAY_BUFFER_BINDING:34965,\n"
"        STREAM_DRAW:35040, STATIC_DRAW:35044, DYNAMIC_DRAW:35048, BUFFER_SIZE:34660, BUFFER_USAGE:34661,\n"
"        CURRENT_VERTEX_ATTRIB:34342,\n"
"        FRONT:1028, BACK:1029, FRONT_AND_BACK:1032, CW:2304, CCW:2305,\n"
"        CULL_FACE:2884, BLEND:3042, DITHER:3024, STENCIL_TEST:2960, DEPTH_TEST:2929, SCISSOR_TEST:3089,\n"
"        POLYGON_OFFSET_FILL:32823, SAMPLE_ALPHA_TO_COVERAGE:32926, SAMPLE_COVERAGE:32928,\n"
"        NO_ERROR:0, INVALID_ENUM:1280, INVALID_VALUE:1281, INVALID_OPERATION:1282, OUT_OF_MEMORY:1285,\n"
"        INVALID_FRAMEBUFFER_OPERATION:1286, CONTEXT_LOST_WEBGL:37442,\n"
"        LINE_WIDTH:2849, ALIASED_POINT_SIZE_RANGE:33901, ALIASED_LINE_WIDTH_RANGE:33902,\n"
"        CULL_FACE_MODE:2885, FRONT_FACE:2886,\n"
"        DEPTH_RANGE:2928, DEPTH_WRITEMASK:2930, DEPTH_CLEAR_VALUE:2931, DEPTH_FUNC:2932,\n"
"        STENCIL_CLEAR_VALUE:2961, STENCIL_FUNC:2962, STENCIL_FAIL:2964, STENCIL_PASS_DEPTH_FAIL:2965,\n"
"        STENCIL_PASS_DEPTH_PASS:2966, STENCIL_REF:2967, STENCIL_VALUE_MASK:2963, STENCIL_WRITEMASK:2968,\n"
"        STENCIL_BACK_FUNC:34816, STENCIL_BACK_FAIL:34817, STENCIL_BACK_PASS_DEPTH_FAIL:34818,\n"
"        STENCIL_BACK_PASS_DEPTH_PASS:34819, STENCIL_BACK_REF:36003, STENCIL_BACK_VALUE_MASK:36004, STENCIL_BACK_WRITEMASK:36005,\n"
"        VIEWPORT:2978, SCISSOR_BOX:3088, COLOR_CLEAR_VALUE:3106, COLOR_WRITEMASK:3107,\n"
"        UNPACK_ALIGNMENT:3317, PACK_ALIGNMENT:3333,\n"
"        MAX_TEXTURE_SIZE:3379, MAX_VIEWPORT_DIMS:3386, MAX_CUBE_MAP_TEXTURE_SIZE:34076,\n"
"        SUBPIXEL_BITS:3408, RED_BITS:3410, GREEN_BITS:3411, BLUE_BITS:3412, ALPHA_BITS:3413,\n"
"        DEPTH_BITS:3414, STENCIL_BITS:3415,\n"
"        POLYGON_OFFSET_UNITS:10752, POLYGON_OFFSET_FACTOR:32824,\n"
"        TEXTURE_BINDING_2D:32873, TEXTURE_BINDING_CUBE_MAP:34068,\n"
"        SAMPLE_BUFFERS:32936, SAMPLES:32937, SAMPLE_COVERAGE_VALUE:32938, SAMPLE_COVERAGE_INVERT:32939,\n"
"        COMPRESSED_TEXTURE_FORMATS:34467,\n"
"        DONT_CARE:4352, FASTEST:4353, NICEST:4354, GENERATE_MIPMAP_HINT:33170,\n"
"        BYTE:5120, UNSIGNED_BYTE:5121, SHORT:5122, UNSIGNED_SHORT:5123, INT:5124, UNSIGNED_INT:5125, FLOAT:5126,\n"
"        DEPTH_COMPONENT:6402, ALPHA:6406, RGB:6407, RGBA:6408, LUMINANCE:6409, LUMINANCE_ALPHA:6410,\n"
"        UNSIGNED_SHORT_4_4_4_4:32819, UNSIGNED_SHORT_5_5_5_1:32820, UNSIGNED_SHORT_5_6_5:33635,\n"
"        FRAGMENT_SHADER:35632, VERTEX_SHADER:35633,\n"
"        MAX_VERTEX_ATTRIBS:34921, MAX_VERTEX_UNIFORM_VECTORS:36347, MAX_VARYING_VECTORS:36348,\n"
"        MAX_COMBINED_TEXTURE_IMAGE_UNITS:35661, MAX_VERTEX_TEXTURE_IMAGE_UNITS:35660,\n"
"        MAX_TEXTURE_IMAGE_UNITS:34930, MAX_FRAGMENT_UNIFORM_VECTORS:36349,\n"
"        SHADER_TYPE:35663, DELETE_STATUS:35712, COMPILE_STATUS:35713, LINK_STATUS:35714,\n"
"        VALIDATE_STATUS:35715, INFO_LOG_LENGTH:35716, ATTACHED_SHADERS:35717,\n"
"        ACTIVE_UNIFORMS:35718, ACTIVE_ATTRIBUTES:35721,\n"
"        SHADING_LANGUAGE_VERSION:35724, CURRENT_PROGRAM:35725,\n"
"        NEVER:512, LESS:513, EQUAL:514, LEQUAL:515, GREATER:516, NOTEQUAL:517, GEQUAL:518, ALWAYS:519,\n"
"        KEEP:7680, REPLACE:7681, INCR:7682, DECR:7683, INVERT:5386, INCR_WRAP:35001, DECR_WRAP:35002,\n"
"        VENDOR:7936, RENDERER:7937, VERSION:7938,\n"
"        NEAREST:9728, LINEAR:9729, NEAREST_MIPMAP_NEAREST:9984, LINEAR_MIPMAP_NEAREST:9985,\n"
"        NEAREST_MIPMAP_LINEAR:9986, LINEAR_MIPMAP_LINEAR:9987,\n"
"        TEXTURE_MAG_FILTER:10240, TEXTURE_MIN_FILTER:10241, TEXTURE_WRAP_S:10242, TEXTURE_WRAP_T:10243,\n"
"        TEXTURE_2D:3553, TEXTURE:5890, TEXTURE_CUBE_MAP:34067,\n"
"        TEXTURE_CUBE_MAP_POSITIVE_X:34069, TEXTURE_CUBE_MAP_NEGATIVE_X:34070,\n"
"        TEXTURE_CUBE_MAP_POSITIVE_Y:34071, TEXTURE_CUBE_MAP_NEGATIVE_Y:34072,\n"
"        TEXTURE_CUBE_MAP_POSITIVE_Z:34073, TEXTURE_CUBE_MAP_NEGATIVE_Z:34074,\n"
"        ACTIVE_TEXTURE:34016, REPEAT:10497, CLAMP_TO_EDGE:33071, MIRRORED_REPEAT:33648,\n"
"        FRAMEBUFFER:36160, RENDERBUFFER:36161, FRAMEBUFFER_BINDING:36006, RENDERBUFFER_BINDING:36007,\n"
"        FRAMEBUFFER_COMPLETE:36053, COLOR_ATTACHMENT0:36064, DEPTH_ATTACHMENT:36096,\n"
"        STENCIL_ATTACHMENT:36128, DEPTH_STENCIL_ATTACHMENT:33306, DEPTH_STENCIL:34041, UNSIGNED_INT_24_8:34042,\n"
"        COMPRESSED_RGB_S3TC_DXT1_EXT:33776, COMPRESSED_RGBA_S3TC_DXT1_EXT:33777,\n"
"        COMPRESSED_RGBA_S3TC_DXT3_EXT:33778, COMPRESSED_RGBA_S3TC_DXT5_EXT:33779\n"
"      };\n"
"      // The native object may refuse new properties (the bridge builds it\n"
"      // on the C side), so the constants live on a prototype child instead:\n"
"      // own props win, everything else still reaches the bridge, and `this`\n"
"      // never matters because the bridge functions ignore it.\n"
"      var w = Object.create(gl);\n"
"      // The bridge reads the canvas id from the opaque of the *exact* object\n"
"      // it created (JS_GetOpaque), so every inherited method must run with\n"
"      // `this` === the native object - bind them, or createShader and friends\n"
"      // silently return 0 and getAttribLocation returns -1.\n"
"      for (var _mk in gl) {\n"
"        var _mv = gl[_mk];\n"
"        if (typeof _mv === 'function') w[_mk] = _mv.bind(gl);\n"
"        else if (w[_mk] === undefined) w[_mk] = _mv;\n"
"      }\n"
"      for (var _gk in NFU_GLC) if (w[_gk] === undefined) w[_gk] = NFU_GLC[_gk];\n"
"      // Draw-call census: a clean frame that clears to a flat colour but submits\n"
"      // no geometry looks identical to a frame that draws geometry off-screen, so\n"
"      // count the submissions to tell the two apart.\n"
"      try {\n"
"        var _dc = { clr: 0, de: 0, da: 0, i1: 0, i2: 0, tot: 0 };\n"
"        var _dcWrap = function(_n, _k) {\n"
"          var _f = w[_n];\n"
"          if (typeof _f !== 'function') return;\n"
"          // The id has to be read here: the wrapper is called as a bare function\n"
"          // (the bound swgl entry takes no `this`), so `this._attrs` threw inside\n"
"          // the census and the try/catch around it swallowed the print. Every\n"
"          // earlier GDRAW=0 readings were therefore a broken probe, not proof\n"
"          // that nothing was drawn.\n"
"          var _wid = '?';\n"
"          try { _wid = (this._attrs && this._attrs['id']) ? String(this._attrs['id']) : '?'; } catch(_we){}\n"
"          w[_n] = function() {\n"
"            _dc[_k]++; _dc.tot++;\n"
"            if (_dc.tot === 1 || _dc.tot % 200 === 0)\n"
"              try { print('[GDRAW] id=' + _wid + ' clr=' + _dc.clr + ' de=' + _dc.de + ' da=' + _dc.da + ' i=' + _dc.i1 + '/' + _dc.i2 + ' tot=' + _dc.tot); } catch(_e){}\n"
"            return _f.apply(null, arguments);\n"
"          };\n"
"        };\n"
"        _dcWrap('clear', 'clr'); _dcWrap('drawElements', 'de'); _dcWrap('drawArrays', 'da');\n"
"        // Babylon submits through the instanced entry points on WebGL2 and on\n"
"        // ANGLE; a census that watched only drawElements/drawArrays reported\n"
"        // zero draws for frames that were drawing everything via instancing.\n"
"        _dcWrap('drawElementsInstanced', 'i1'); _dcWrap('drawArraysInstanced', 'i2');\n"
"        _dcWrap('drawElementsInstancedANGLE', 'i1'); _dcWrap('drawArraysInstancedANGLE', 'i2');\n"
"        _dcWrap('drawRangeElements', 'de'); _dcWrap('multiDrawElementsWEBGL', 'de');\n"
"        // gl.clear() with COLOR_BUFFER_BIT is the frame door: report the very\n"
"        // first one so the census has a timestamp even if nothing is ever drawn.\n"
"        try { print('[GLCLEAR-READY] tbl=' + (typeof w.createBuffer)); } catch(_e2){}\n"
"      } catch(_e3){}\n"
"      try { print('[GLW] id=' + this._attrs['id'] + ' k=' + (function(){var n=0;for(var q in gl)n++;return n;}()) + ' clear=' + typeof w.clear + ' clearColor=' + typeof w.clearColor + ' clearDepth=' + typeof w.clearDepth); } catch(eg1){}\n"
"      // Babylon validates a pile of GL capabilities right after it boots. If any\n"
"      // of them answers null or a wrong shape the engine aborts, and bloxd turns\n"
"      // that into INITIALISE_WEBGL and walks back to the home page. Log every\n"
"      // extension/parameter the game canvas asks for (capped) so the offending\n"
"      // query is visible instead of guessed at.\n"
"      try {\n"
"        if (this._attrs['id'] === 'noa-canvas') {\n"
"          var _cap = { ext: 0, par: 0, shp: 0 };\n"
"          var _gx0 = w.getExtension;\n"
"          if (typeof _gx0 === 'function') w.getExtension = function(n){\n"
"            var r = _gx0.apply(null, arguments);\n"
"            if (_cap.ext < 80) { _cap.ext++; try { print('[GLEXT] ' + n + ' -> ' + (r ? 'obj' : 'null')); } catch(e){} }\n"
"            return r; };\n"
"          var _gp0 = w.getParameter;\n"
"          if (typeof _gp0 === 'function') w.getParameter = function(p){\n"
"            var r = _gp0.apply(null, arguments);\n"
"            if (_cap.par < 120) { _cap.par++; try { print('[GLPAR] ' + p + ' -> ' + (r === null ? 'null' : String(r).slice(0, 40))); } catch(e){} }\n"
"            return r; };\n"
"          var _gs0 = w.getShaderParameter;\n"
"          if (typeof _gs0 === 'function') w.getShaderParameter = function(sh, p){\n"
"            var r = _gs0.apply(null, arguments);\n"
"            if (_cap.shp < 40) { _cap.shp++; try { print('[GLSHP] p=' + p + ' -> ' + String(r)); } catch(e){} }\n"
"            return r; };\n"
"          var _ex0 = w.getError;\n"
"          if (typeof _ex0 === 'function') w.getError = function(){\n"
"            var r = _ex0.apply(null, arguments);\n"
"            if (r) try { print('[GLERR] code=' + r); } catch(e){}\n"
"            return r; };\n"
"          // Which GL entry points the renderer actually touches: a context that\n"
"          // never sees bufferData/drawElements was created but never used, and\n"
"          // no amount of waiting for the world will change that.\n"
"          var _cc = { seen: 0, names: '', tot: 0 };\n"
"          var _keys0 = [];\n"
"          try { for (var _k0 in w) _keys0.push(_k0); } catch(_k0e){}\n"
"          for (var _ci = 0; _ci < _keys0.length; ++_ci) {\n"
"            (function(_nm){\n"
"              var _fn = w[_nm];\n"
"              if (typeof _fn !== 'function') return;\n"
"              w[_nm] = function(){\n"
"                _cc.tot++;\n"
"                if (_cc.names.indexOf('|' + _nm + '|') < 0 && _cc.seen < 60) {\n"
"                  _cc.seen++; _cc.names += '|' + _nm + '|';\n"
"                  try { print('[GLCALL] first ' + _nm + ' tot=' + _cc.tot); } catch(_c1){}\n"
"                }\n"
"                return _fn.apply(null, arguments);\n"
"              };\n"
"            })(_keys0[_ci]);\n"
"          }\n"
"          try { print('[GLCALL-ARMED] n=' + _keys0.length); } catch(_c2){}\n"
"          var _tw = 0;\n"
"          var _keys = [];\n"
"          try { for (var _k2 in w) _keys.push(_k2); } catch(_ke){}\n"
"          for (var _ki = 0; _ki < _keys.length; ++_ki) {\n"
"            (function(_nm){\n"
"              var _fn = w[_nm];\n"
"              if (typeof _fn !== 'function') return;\n"
"              w[_nm] = function(){\n"
"                try { return _fn.apply(null, arguments); }\n"
"                catch(_er){\n"
"                  if (_tw < 20) { _tw++; try { print('[GLTHROW] ' + _nm + ' msg=' + String(_er && _er.message ? _er.message : _er).slice(0, 120)); } catch(_pe){} }\n"
"                  throw _er;\n"
"                }\n"
"              };\n"
"            })(_keys[_ki]);\n"
"          }\n"
"          try { print('[GLTHROW-ARMED] n=' + _keys.length); } catch(_ae){}\n"
"        }\n"
"      } catch(_ecap){}\n"
"      // Screen-orientation inputs: bloxd gates its \"Enter Bloxd\" entry on these,\n"
"      // so what the page believes about orientation decides whether the game can\n"
"      // be entered at all.\n"
"      try {\n"
"        var _vw = (typeof g.innerWidth === 'number') ? g.innerWidth : -1;\n"
"        var _vh = (typeof g.innerHeight === 'number') ? g.innerHeight : -1;\n"
"        var _po = '?', _la = '?';\n"
"        try { _po = String(g.matchMedia('(orientation: portrait)').matches); } catch(_p1){}\n"
"        try { _la = String(g.matchMedia('(orientation: landscape)').matches); } catch(_l1){}\n"
"        var _so = '?';\n"
"        try { _so = String(g.screen && g.screen.orientation && g.screen.orientation.type); } catch(_s1){}\n"
"        var _touch = '?';\n"
"        try { _touch = String(g.__nfuTouch === undefined ? ('ontouchstart' in g) : g.__nfuTouch); } catch(_t1){}\n"
"        print('[ORIENT] iw=' + _vw + ' ih=' + _vh + ' portrait=' + _po + ' landscape=' + _la + ' so=' + _so + ' touch=' + _touch);\n"
"      } catch(eo){}\n"
"      this.__glw = w;\n"
"      return w;\n"
"    }\n"
"    // Unknown context types (bloxd probes 'experimental-webgl2' before\n"
"    // 'webgl') must answer null like a real browser. Falling through to the\n"
"    // 2d stub handed back an object without getExtension, which GPU-info\n"
"    // probes then called and the whole engine-setup chain died.\n"
"    if (type === '2d') { if (!this.__ctx2d) this.__ctx2d = _mkCtx2d.call(this); return this.__ctx2d; }\n"
"    return null;\n"
"  };\n"
"  // ---- WebSocket (RFC 6455): thin wrapper over the native client. The\n"
"  // per-frame pump delivers open/message/close so a game loop keeps running.\n"
"  var _wsAll = [];\n"
"  function _wsUtf8(ab){\n"
"    var u = new Uint8Array(ab), s = '', i = 0, c;\n"
"    while (i < u.length){\n"
"      c = u[i++];\n"
"      if (c < 128) s += String.fromCharCode(c);\n"
"      else if (c < 224) s += String.fromCharCode(((c & 31) << 6) | (u[i++] & 63));\n"
"      else if (c < 240) s += String.fromCharCode(((c & 15) << 12) | ((u[i++] & 63) << 6) | (u[i++] & 63));\n"
"      else { var cp = (((c & 7) << 18) | ((u[i++] & 63) << 12) | ((u[i++] & 63) << 6) | (u[i++] & 63)) - 0x10000;\n"
"             s += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 1023)); }\n"
"    }\n"
"    return s;\n"
"  }\n"
"  function _wsBlob(ab){\n"
"    return { _ab: ab, size: ab.byteLength, type: '',\n"
"             arrayBuffer: function(){ return Promise.resolve(ab); },\n"
"             text: function(){ return Promise.resolve(_wsUtf8(ab)); },\n"
"             slice: function(){ return _wsBlob(ab); } };\n"
"  }\n"
"  function _wsFire(ws, type, ev){\n"
"    if (!ev) ev = {};\n"
"    ev.type = type; ev.target = ws; ev.currentTarget = ws;\n"
"    var a = ws._ls && ws._ls[type], h = ws['on' + type], i;\n"
"    // A swallowed handler error used to hide the real reason the room join\n"
"    // never sent its JOIN frame, so surface the first few.\n"
"    var _eh = function(e, kind){\n"
"      try {\n"
"        if ((ws._ehN = (ws._ehN || 0) + 1) > 12) return;\n"
"        print('[WSEH] h=' + ws._h + ' ev=' + type + ' ' + kind + ' ' + String(e && e.message ? e.message : e));\n"
"      } catch(e2){}\n"
"    };\n"
"    if (a) for (i = 0; i < a.length; i++){ try { a[i].call(ws, ev); } catch(e){ _eh(e, 'ls'); } }\n"
"    if (typeof h === 'function'){ try { h.call(ws, ev); } catch(e){ _eh(e, 'on'); } }\n"
"    return true;\n"
"  }\n"
"  function _wsClosed(ws, code, reason){\n"
"    if (ws.readyState === 3) return;\n"
"    // The close code is the only thing that separates \"the peer closed us\"\n"
"    // (1000/1005/1011) from \"our own receive path died\" (1006), so record it.\n"
"    try { print('[WSX] h=' + ws._h + ' code=' + code + ' reason=' + String(reason || '').slice(0, 80) + ' sent=' + (ws._sent ? 1 : 0)); } catch(wx0){}\n"
"    ws.readyState = 3; ws._dead = true;\n"
"    try { if (typeof g.__nfuWsClose === 'function' && ws._h >= 0) g.__nfuWsClose(ws._h); } catch(e){}\n"
"    _wsFire(ws, 'close', { code: code, reason: reason, wasClean: code === 1000 });\n"
"  }\n"
"  g.__nfuWsPump = function(){\n"
"    g.__nfuWsPumpCalls = (g.__nfuWsPumpCalls || 0) + 1;\n"
"    if (g.__nfuWsPumpCalls % 50 === 1) try { print('[WPU] calls=' + g.__nfuWsPumpCalls + ' sockets=' + _wsAll.length + ' h=[' + _wsAll.map(function(w){ return (w._h + ':' + w.readyState + ':' + (w._sent ? 'S' : '-') + ':' + (w._dead ? 'D' : '-')); }).join(',') + ']'); } catch(wu0){}\n"
"    // Frame-rate witness: the pump runs once per browser frame, so the call\n"
"    // count against wall clock is the only way to tell a slow software raster\n"
"    // from a stalled page loop without a debugger.\n"
"    if (g.__nfuWsPumpCalls % 500 === 1) try { var _wpn = (g.__nfuWsPumpN = (g.__nfuWsPumpN || 0) + 1); print('[WPF] calls=' + g.__nfuWsPumpCalls + ' mark=' + _wpn + ' ms=' + Date.now()); } catch(wf0){}\n"
"    var i, list = _wsAll;\n"
"    for (i = 0; i < list.length; i++){\n"
"      var ws = list[i];\n"
"      if (!ws || ws._dead || ws._h < 0) continue;\n"
"      var st = 2;\n"
"      try { st = g.__nfuWsState(ws._h); } catch(e){ st = 2; }\n"
"      if (ws.readyState === 0 && (st === 1 || st === 2)){ ws.readyState = 1; try { print('[WSEV] h=' + ws._h + ' open fired dt=' + (Date.now() - (ws._t0 || 0)) + ' hs=' + ((ws._ls && ws._ls['open'] ? ws._ls['open'].length : 0)) + ' on=' + (typeof ws.onopen)); } catch(we0){} _wsFire(ws, 'open', {}); }\n"
"      // The room handshake is server-driven: the game server sends JOIN_ROOM\n"
"      // (0x0a) itself and colyseus answers it from onMessageCallback, so an\n"
"      // injected JOIN frame only puts a stray byte on the wire ahead of the\n"
"      // real one. With the upgrade headers fixed the reply happens on its own.\n"
"      var guard = 0;\n"
"      while (guard++ < 64){\n"
"        var m;\n"
"        try { m = g.__nfuWsPoll(ws._h); } catch(e){ break; }\n"
"        if (m === undefined) break;\n"
"        if (m === null){ _wsClosed(ws, 1006, ''); break; }\n"
"        var d = (typeof m === 'string') ? m : (ws.binaryType === 'arraybuffer' ? m : _wsBlob(m));\n"
"        g.__nfuWsMsgN = (g.__nfuWsMsgN || 0) + 1;\n"
"        var _hexOf = function(v){\n"
"          try {\n"
"            var u = null;\n"
"            if (typeof v === 'string') { var ss = ''; for (var q = 0; q < v.length && q < 40; q++) ss += v.charCodeAt(q).toString(16).padStart(2,'0'); return ss; }\n"
"            if (v && v.buffer instanceof ArrayBuffer) u = new Uint8Array(v.buffer, v.byteOffset || 0, Math.min(v.byteLength || 0, 40));\n"
"            else if (v && typeof v.byteLength === 'number') u = new Uint8Array(v, 0, Math.min(v.byteLength, 40));\n"
"            else if (v && v._ab) u = new Uint8Array(v._ab, 0, Math.min(v._ab.byteLength || 0, 40));\n"
"            if (!u) return '?';\n"
"            var hx = ''; for (var i2 = 0; i2 < u.length; i2++) hx += u[i2].toString(16).padStart(2,'0');\n"
"            return hx;\n"
"          } catch(e){ return 'E'; }\n"
"        };\n"
"        // Full-frame dump for the first few frames: the handshake/join bytes are\n"
"        // the only place where our client can differ from a real browser, and a\n"
"        // 20-byte prefix hides exactly the divergence point.\n"
"        try {\n"
"          var _ful = '';\n"
"          try {\n"
"            var _fv = d;\n"
"            var _u8 = null;\n"
"            if (typeof _fv === 'string') { _u8 = []; for (var q2 = 0; q2 < _fv.length && q2 < 1200; q2++) _u8.push(_fv.charCodeAt(q2) & 255); }\n"
"            else if (_fv && _fv.buffer instanceof ArrayBuffer) { var _t = new Uint8Array(_fv.buffer, _fv.byteOffset || 0, Math.min(_fv.byteLength || 0, 1200)); for (var q3 = 0; q3 < _t.length; q3++) _u8.push(_t[q3]); }\n"
"            else if (_fv && typeof _fv.byteLength === 'number') { var _t2 = new Uint8Array(_fv, 0, Math.min(_fv.byteLength, 1200)); for (var q4 = 0; q4 < _t2.length; q4++) _u8.push(_t2[q4]); }\n"
"          } catch(fd0){}\n"
"          if (_u8 && _u8.length) { for (var q5 = 0; q5 < _u8.length; q5++) _ful += (_u8[q5] < 16 ? '0' : '') + _u8[q5].toString(16); }\n"
"          if (_ful && g.__nfuWsMsgN <= 14) { for (var q6 = 0; q6 < _ful.length; q6 += 180) print('[WSHR] h=' + ws._h + ' n=' + g.__nfuWsMsgN + ' o=' + q6 + ' ' + _ful.substr(q6, 180)); }\n"
"        } catch(fe0){}\n"
"        if (g.__nfuWsMsgN <= 40) try {\n"
"          var _wn = 0; try { _wn = (typeof d === 'string') ? d.length : (d && d.byteLength) || 0; } catch(_wl){};\n"
"          print('[WSM] h=' + ws._h + ' n=' + g.__nfuWsMsgN + ' len=' + _wn + ' ty=' + ((typeof d === 'string') ? 'str' : 'bin') + ' hex=' + _hexOf(d));\n"
"        } catch(pe){}\n"
"        _wsFire(ws, 'message', { data: d, origin: '', lastEventId: '' });\n"
"        if (ws.readyState !== 1) break;\n"
"      }\n"
"      if (st === 2 && ws.readyState !== 3) _wsClosed(ws, 1005, '');\n"
"    }\n"
"  };\n"
"  g.WebSocket = function(url, protocols){\n"
"    var self = this;\n"
"    try {\n"
"      var _pj = '';\n"
"      try { _pj = (protocols === undefined) ? 'undef' : ((typeof protocols === 'string') ? protocols : JSON.stringify(protocols)).slice(0, 400); } catch(pj){ _pj = 'unser'; }\n"
"      print('[WS] ctor ' + String(url) + ' proto=' + _pj);\n"
"    } catch(e){}\n"
"    this.url = String(url);\n"
"    this.readyState = 0; this.bufferedAmount = 0;\n"
"    this.extensions = ''; this.protocol = protocols ? String(protocols) : '';\n"
"    this.binaryType = 'blob';\n"
"    this.onopen = null; this.onmessage = null; this.onerror = null; this.onclose = null;\n"
"    this._ls = {}; this._h = -1; this._dead = false; this._t0 = Date.now();\n"
"    if (typeof g.__nfuWsOpen !== 'function'){\n"
"      setTimeout(function(){ _wsClosed(self, 1006, 'unsupported'); }, 0);\n"
"      return;\n"
"    }\n"
"    var h = -1;\n"
"    var _wsCookieHdr = '';\n"
"    // Browsers always send Origin and a UA on the upgrade; bloxd's game\n"
"    // servers close the socket immediately (page side: 1006) when Origin is\n"
"    // absent, which read as a dead room join. Cookie still comes from the jar.\n"
"    try {\n"
"      var _ck = '';\n"
"      try { _ck = g.document && g.document.cookie ? g.document.cookie : ''; } catch(ce0){}\n"
"      _wsCookieHdr = 'Origin: https://bloxd.io\\r\\n'\n"
"                  + 'Referer: https://bloxd.io/play/survival\\r\\n'\n"
"                  + 'Accept-Language: en-US,en;q=0.9\\r\\n'\n"
"                  + 'Cache-Control: no-cache\\r\\n'\n"
"                  + 'User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36\\r\\n'\n"
"                  + (_ck ? ('Cookie: ' + _ck + '\\r\\n') : '');\n"
"    } catch(ce){}\n"
"    try { h = g.__nfuWsOpen(this.url, _wsCookieHdr); } catch(e){ h = -1; }\n"
"    try { print('[WS] opened h=' + h + ' hdr=' + _wsCookieHdr.length + ' ' + String(url)); } catch(e){}\n"
"    if (h < 0){\n"
"      setTimeout(function(){ _wsFire(self, 'error', {}); _wsClosed(self, 1006, 'connect failed'); }, 0);\n"
"      return;\n"
"    }\n"
"    this._h = h;\n"
"    _wsAll.push(this);\n"
"  };\n"
"  g.WebSocket.prototype = {\n"
"    CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3,   // legacy constants on instances\n"
"    send: function(d){\n"
"      // Every attempt is logged (not just the successful ones): a JOIN frame the\n"
"      // app tried to send while our readyState was still CONNECTING vanished here.\n"
"      try { if ((g.__nfuWsTryN = (g.__nfuWsTryN || 0) + 1) <= 30) print('[WSEND] h=' + this._h + ' rs=' + this.readyState + ' ty=' + (typeof d) + ' n=' + (typeof d === 'string' ? d.length : (d && d.byteLength))); } catch(se0){}\n"
"      if (this.readyState !== 1) return;\n"
"      var v = d, bin = 1;\n"
"      if (typeof v === 'string'){ bin = 0; }\n"
"      else if (v instanceof ArrayBuffer){}\n"
"      else if (v && v.buffer instanceof ArrayBuffer){\n"
"        v = v.buffer.slice(v.byteOffset || 0, (v.byteOffset || 0) + v.byteLength);\n"
"      } else { v = String(d); bin = 0; }\n"
"      this._sent = 1;\n"
"      var _sret = 0;\n"
"      try { _sret = g.__nfuWsSend(this._h, v, bin); } catch(e){ _sret = -3; }\n"
"      if (_sret < 0) try { print('[WSENDFAIL] h=' + this._h + ' r=' + _sret + ' len=' + (typeof v === 'string' ? v.length : v.byteLength) + ' st=' + g.__nfuWsState(this._h)); } catch(sf){};\n"
"      g.__nfuWsSendN = (g.__nfuWsSendN || 0) + 1;\n"
"      if (g.__nfuWsSendN <= 40) try {\n"
"        var _sh = '';\n"
"        try {\n"
"          if (typeof v === 'string') { for (var _k = 0; _k < v.length && _k < 40; _k++) _sh += v.charCodeAt(_k).toString(16).padStart(2,'0'); }\n"
"          else { var _u = new Uint8Array(v); for (var _k2 = 0; _k2 < _u.length && _k2 < 40; _k2++) _sh += _u[_k2].toString(16).padStart(2,'0'); }\n"
"        } catch(_se){}\n"
"        print('[WSS] h=' + this._h + ' blen=' + bin + ' len=' + (typeof v === 'string' ? v.length : (v && v.byteLength !== undefined ? v.byteLength : -1)) + ' hex=' + _sh);\n"
"        try { if (g.__nfuWsSendN <= 14 && _sh.length > 40 && typeof v !== 'string') { var _big = new Uint8Array(v); var _bh2 = ''; for (var _k3 = 0; _k3 < _big.length && _k3 < 1200; _k3++) _bh2 += (_big[_k3] < 16 ? '0' : '') + _big[_k3].toString(16); for (var _k4 = 0; _k4 < _bh2.length; _k4 += 180) print('[WSHS] h=' + this._h + ' n=' + g.__nfuWsSendN + ' o=' + _k4 + ' ' + _bh2.substr(_k4, 180)); } } catch(_bs){}\n"
"      } catch(pe){}\n"
"      // Stack probe: the first few binary sends tell us which app-level call\n"
"      // actually pushes bytes onto the wire. A 22-byte payload after JOIN_ROOM\n"
"      // is far too small for bloxd's join blob, so we need the caller identity.\n"
"      try {\n"
"        if (bin) {\n"
"          g.__nfuWsStk = (g.__nfuWsStk || 0) + 1;\n"
"          if (g.__nfuWsStk <= 3) {\n"
"            var _st = '';\n"
"            try { _st = String(new Error('snd').stack || '').replace(/\\n/g, ' | ').slice(0, 900); } catch(_e2){}\n"
"            print('[WSS-ST] n=' + g.__nfuWsStk + ' ' + _st);\n"
"          }\n"
"        }\n"
"      } catch(pe2){}\n"
"    },\n"
"    close: function(code, reason){\n"
"      if (this.readyState === 3) return;\n"
"      this.readyState = 2;\n"
"      try { if (typeof g.__nfuWsClose === 'function' && this._h >= 0) g.__nfuWsClose(this._h); } catch(e){}\n"
"      _wsClosed(this, code || 1000, reason || '');\n"
"    },\n"
"    addEventListener: function(t, f){\n"
"      try { if (this._h >= 0 && (this._attN = (this._attN || 0) + 1) <= 10) print('[WSATT] h=' + this._h + ' t=' + t + ' fn=' + (typeof f) + ' rs=' + this.readyState); } catch(ae){}\n"
"      if (!this._ls[t]) this._ls[t] = []; this._ls[t].push(f);\n"
"    },\n"
"    removeEventListener: function(t, f){ var a = this._ls[t]; if (a){ var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } },\n"
"    dispatchEvent: function(ev){ _wsFire(this, (ev && ev.type) || '', ev || {}); return true; }\n"
"  };\n"
"  g.WebSocket.CONNECTING = 0; g.WebSocket.OPEN = 1;\n"
"  g.WebSocket.CLOSING = 2; g.WebSocket.CLOSED = 3;\n"
"  // ---- ReadableStream ---------------------------------------------------\n"
"  // bloxd's asset pump builds streams itself (new ReadableStream({start})) and\n"
"  // the loader reported \"Loading required libraries 0%\" forever because the\n"
"  // global constructor was missing. Minimal but semantically faithful: the\n"
"  // controller may enqueue synchronously or later; read() resolves the queued\n"
"  // chunks in order and reports done after close(). The payload is already\n"
"  // materialised by the fetch pump, so no real backpressure is needed.\n"
"  g.ReadableStream = function(src){\n"
"    var self = this; this._q = []; this._done = false; this._err = null; this._locked = false;\n"
"    this._waiters = [];\n"
"    if (src && typeof src.start === 'function'){\n"
"      var ctl = {\n"
"        enqueue: function(v){ self._q.push(v); for (var i = 0; i < self._waiters.length; i++) try { self._waiters[i](); } catch(e){} self._waiters.length = 0; },\n"
"        close: function(){ self._done = true; for (var i = 0; i < self._waiters.length; i++) try { self._waiters[i](); } catch(e){} self._waiters.length = 0; },\n"
"        error: function(e){ self._err = e; self._done = true; for (var i = 0; i < self._waiters.length; i++) try { self._waiters[i](); } catch(x){} self._waiters.length = 0; }\n"
"      };\n"
"      try { src.start(ctl); } catch(e){ this._err = e; this._done = true; }\n"
"    }\n"
"  };\n"
"  g.ReadableStream.prototype.getReader = function(){\n"
"    if (this._locked) throw new Error('ReadableStream already locked');\n"
"    this._locked = true;\n"
"    return new g.ReadableStreamDefaultReader(this);\n"
"  };\n"
"  g.ReadableStream.prototype.cancel = function(){ this._done = true; return Promise.resolve(); };\n"
"  g.ReadableStream.prototype.tee = function(){ var a = this, b = new g.ReadableStream(); return [a, b]; };\n"
"  g.ReadableStreamDefaultReader = function(stream){ this._s = stream; this.closed = false; };\n"
"  g.ReadableStreamDefaultReader.prototype.read = function(){\n"
"    var s = this._s, self = this;\n"
"    return new Promise(function(res, rej){\n"
"      function pump(){\n"
"        if (s._err) { self.closed = true; rej(s._err); return; }\n"
"        if (!s._q) { self.closed = true; res({ value: undefined, done: true }); return; }\n"
"        if (s._q.length) { res({ value: s._q.shift(), done: false }); return; }\n"
"        if (s._done) { self.closed = true; res({ value: undefined, done: true }); return; }\n"
"        s._waiters.push(pump);\n"
"      }\n"
"      pump();\n"
"    });\n"
"  };\n"
"  g.ReadableStreamDefaultReader.prototype.cancel = function(){ this._s._done = true; return Promise.resolve(); };\n"
"  g.ReadableStreamDefaultReader.prototype.releaseLock = function(){ this._s._locked = false; };\n"
"  // ResizeObserverEntry: observed code reads entry.contentRect / target inside\n"
"  // its callback; the class itself was missing even though the observer works.\n"
"  g.ResizeObserverEntry = g.ResizeObserverEntry || function(t, r){ this.target = t; this.contentRect = r; this.borderBoxSize = [{ inlineSize: r && r.width, blockSize: r && r.height }]; this.contentBoxSize = this.borderBoxSize; };\n"
  // Binary-safe fetch. The old version went through __nfuHttpGet, which only
  // returns decoded TEXT: every GLB model and block texture arrived as mojibake
  // and the page rejected it ("GLB refused: it is not a binary glTF 2.0 file").
  // __nfuHttpRequest returns {status, body, hdr, bodyAB}, and bodyAB is a real
  // ArrayBuffer for binary content types, so arrayBuffer()/bytes()/body give the
  // page the bytes it actually asked for.
"  g.fetch = function(u, o){ var url = String(u); var opt = o || {}; try { print('[FETCH] ' + String(url).slice(0, 90)); } catch(e){} return new Promise(function(res, rej){ (g.__nfuTimers = g.__nfuTimers || []).push(function(){\n"
"    var status = 0, body = '', hdr = '', ab = null, r = null;\n"
"    try { if (typeof __nfuHttpRequest === 'function') r = __nfuHttpRequest(String(opt.method || 'GET'), url, opt.body === undefined || opt.body === null ? '' : String(opt.body), ''); } catch(e){ r = null; }\n"
"    if (r) { status = r.status | 0; body = r.body === undefined || r.body === null ? '' : String(r.body); hdr = r.hdr || ''; ab = r.bodyAB || null; }\n"
"    else { try { body = String(__nfuHttpGet(url)); status = 200; } catch(e2){ try { print('[FETCH-FAIL] ' + url + ' :: ' + (e2 && e2.message ? e2.message : e2)); } catch(pe){} rej(e2); return; } }\n"
"    var hmap = {}; var hl = String(hdr || '').split(/\\r?\\n/);\n"
"    for (var hi = 0; hi < hl.length; hi++){ var ci = hl[hi].indexOf(':'); if (ci > 0) { var hk = hl[hi].slice(0, ci).trim().toLowerCase(); if (!(hk in hmap)) hmap[hk] = hl[hi].slice(ci + 1).trim(); } }\n"
"    var hdrs = { get: function(n){ var k = String(n).toLowerCase(); return (k in hmap) ? hmap[k] : null; }, has: function(n){ return String(n).toLowerCase() in hmap; }, forEach: function(cb){ for (var k in hmap) try { cb(hmap[k], k); } catch(e){} } };\n"
"    var _ab = function(){ if (ab) { try { return ab.slice(0); } catch(e){} } try { var bb = _toBytes(body); return bb.buffer.slice(bb.byteOffset || 0, (bb.byteOffset || 0) + bb.byteLength); } catch(e){ return new ArrayBuffer(0); } };\n"
"    var resp = {\n"
"      ok: status >= 200 && status < 300, status: status, statusText: status === 200 ? 'OK' : '', url: url, headers: hdrs, redirected: false,\n"
"      text: function(){ return Promise.resolve(body); },\n"
"      json: function(){ try { return Promise.resolve(JSON.parse(body)); } catch(e){ return Promise.reject(e); } },\n"
"      arrayBuffer: function(){ return Promise.resolve(_ab()); },\n"
"      bytes: function(){ return Promise.resolve(new Uint8Array(_ab())); },\n"
"      clone: function(){ return resp; },\n"
"      body: new g.ReadableStream({ start: function(cc){ try { var v = new Uint8Array(_ab()); if (v && v.length) cc.enqueue(v); } catch(e){} try { cc.close(); } catch(e){} } }),\n"
"      blob: function(){ var t = hdrs.get('content-type') || ''; return Promise.resolve({ _srcUrl: url, type: t, size: body.length, text: function(){ return Promise.resolve(body); }, arrayBuffer: function(){ return Promise.resolve(_ab()); }, bytes: function(){ return Promise.resolve(new Uint8Array(_ab())); } }); }\n"
"    };\n"
"    if (status === 0) { try { print('[FETCH-STATUS0] ' + url); } catch(pe){} rej(new Error('Failed to fetch ' + url)); return; }\n"
"    res(resp);\n"
"  }); }); };\n"
  "g.XMLHttpRequest = function(){ this.readyState = 0; this.status = 0; this.statusText = '';\n"
  "  this.responseText = ''; this.response = null; this.responseType = '';\n"
  "  this.withCredentials = false; this.timeout = 0; this.upload = { addEventListener: function(){}, removeEventListener: function(){} };\n"
  "  this._url = ''; this._hdr = {}; this._rh = {}; };\n"
  "g.XMLHttpRequest.UNSENT = 0; g.XMLHttpRequest.OPENED = 1; g.XMLHttpRequest.DONE = 4;\n"
  "g.XMLHttpRequest.prototype = {\n"
  "  UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4,\n"
  "  open: function(m, u){ this._method = String(m || 'GET'); this._url = String(u); this._body = null; this._aborted = 0; g.__nfuXhrN = (g.__nfuXhrN || 0) + 1; if (g.__nfuXhrN <= 200) try { print('[XHR] ' + this._method + ' ' + String(u).slice(0, 120)); } catch(xe){} },\n"
  "  setRequestHeader: function(k, v){ this._rh[String(k)] = String(v); },\n"
  "  getResponseHeader: function(k){ var v = this._hdr[String(k).toLowerCase()]; return v === undefined ? null : v; },\n"
  "  getAllResponseHeaders: function(){ var o = ''; for (var k in this._hdr) o += k + ': ' + this._hdr[k] + '\\r\\n'; return o; },\n"
  "  overrideMimeType: function(){},\n"
  "  abort: function(){ this._aborted = 1; },\n"
  "  addEventListener: function(t, f){ var k = 'on' + t; if (!this[k]) this[k] = f; },\n"
  "  removeEventListener: function(){},\n"
  "  send: function(body){\n"
  "    var self = this;\n"
  "    (g.__nfuTimers = g.__nfuTimers || []).push(function(){\n"
  "      if (self._aborted) return;\n"
  "      var txt = '', ab = null, hdrs = '', status = 200, okcall = 1;\n"
  "      if (/\\/auth\\/(anonymous|userdata|login)/.test(self._url)) {\n"
  "        txt = g.__nfuIdentityJson();\n"
  "        hdrs = 'content-type: application/json\\r\\n';\n"
  "      } else if (/index\\/metrics\\/cookies/.test(self._url)) {\n"
  "        txt = '{}';\n"
  "        hdrs = 'content-type: application/json\\r\\n';\n"
  "      } else {\n"
  "        var r = null;\n"
        // (moved)
  "        try {\n"
  "          if (typeof g.__nfuHttpRequest === 'function')\n"
  "            r = g.__nfuHttpRequest(self._method, self._url, body === undefined || body === null ? '' : (typeof body === 'string' ? body : String(body)), '');\n"
  "        } catch(e){ try { print('[XHR-FAIL] ' + self._url + ' :: ' + (e && e.message ? e.message : e)); } catch(pe){} }\n"
  "        if (r) { status = r.status | 0; hdrs = String(r.hdr || ''); ab = r.bodyAB || null;\n"
  "                  txt = (r.body === undefined || r.body === null) ? '' : String(r.body); }\n"
  "        else { status = 0; okcall = 0; }\n"
  "      }\n"
  "      self._hdr = {};\n"
  "      var hl = String(hdrs || '').split(/\\r?\\n/);\n"
  "      for (var hi = 0; hi < hl.length; hi++){\n"
  "        var ci = hl[hi].indexOf(':');\n"
  "        if (ci > 0){ var hk = hl[hi].slice(0, ci).trim().toLowerCase();\n"
  "          if (!(hk in self._hdr)) self._hdr[hk] = hl[hi].slice(ci + 1).trim(); }\n"
  "      }\n"
  "      self.status = status;\n"
  "      self.statusText = status === 200 ? 'OK' : '';\n"
  "      self.readyState = 4;\n"
  "      // responseType decides what `response` is: Babylon's loader asks for\n"
  "      // 'arraybuffer' and would otherwise receive the decoded text, which is\n"
  "      // exactly how every GLB model got rejected as \"not a binary glTF\".\n"
  "      if (self.responseType === 'arraybuffer'){\n"
  "        if (ab) { try { self.response = ab.slice(0); } catch(_ra){ self.response = ab; } }\n"
  "        else {\n"
  "          var bb = new Uint8Array(txt.length), bi = 0, bs = String(txt);\n"
  "          for (var ci2 = 0; ci2 < bs.length; ci2++){\n"
  "            var cc = bs.charCodeAt(ci2);\n"
  "            if (cc < 128) bb[bi++] = cc;\n"
  "            else if (cc < 2048) { bb[bi++] = 192 | (cc >> 6); bb[bi++] = 128 | (cc & 63); }\n"
  "            else { bb[bi++] = 224 | (cc >> 12); bb[bi++] = 128 | ((cc >> 6) & 63); bb[bi++] = 128 | (cc & 63); }\n"
  "          }\n"
  "          self.response = bb.buffer;\n"
  "        }\n"
  "      } else if (self.responseType === 'json'){\n"
  "        try { self.response = JSON.parse(txt); } catch(_rj){ self.response = null; }\n"
  "      } else if (self.responseType === 'blob' || self.responseType === 'document'){\n"
  "        self.response = txt;\n"
  "      } else {\n"
  "        self.response = txt;\n"
  "      }\n"
  "      self.responseText = txt;\n"
  "      if (self.onreadystatechange) try { self.onreadystatechange(); } catch(e){}\n"
  "      if (self.onload) try { self.onload(); } catch(e){}\n"
  "      if (self.onloadend) try { self.onloadend(); } catch(e){}\n"
  "      if (!okcall) { if (self.onerror) try { self.onerror(); } catch(e){} }\n"
  "    });\n"
  "  }\n"
  "};\n"
  "\n"
"  g.XMLHttpRequest.prototype = { open: function(m, u){ this._method = String(m || 'GET'); this._url = String(u); try { print('[XHR] ' + this._method + ' ' + this._url.slice(0, 130)); } catch(e){} }, send: function(){ var self = this; (g.__nfuTimers = g.__nfuTimers || []).push(function(){ var t = '', hdrs = '';\n      // Colyseus auth emulation: bloxd signs in anonymously via XHR POST\n      // {path}/anonymous then reads {path}/userdata. Serve a minimal JSON\n      // identity so the app can reach its main menu offline.\n      if (/\\/auth\\/(anonymous|userdata|login)/.test(self._url)) {\n        t = g.__nfuIdentityJson();\n        hdrs = 'content-type: application/json\\r\\n';\n      } else if (/index\\/metrics\\/cookies/.test(self._url)) {\n        // server only accepts POST; the GET probe returns 404 HTML which fails\n        // the metrics flush and blocks anonymous sign-in. bloxd's destructure\n        // has fallbacks for every field, so {} is a valid minimal response.\n        t = '{}';\n        hdrs = 'content-type: application/json\\r\\n';\n      } else {\n      try { t = String(__nfuHttpGet(self._url)); } catch(e){ try { print('[XHR-FAIL] ' + self._url + ' :: ' + (e && e.message ? e.message : e)); } catch(pe){} } }\n      self.getAllResponseHeaders = function(){ return hdrs; };\n      self.readyState = 4; self.status = 200; self.responseText = t; self.response = t; if (self.onreadystatechange) try { self.onreadystatechange(); } catch(e){} if (self.onload) try { self.onload(); } catch(e){} }); }, setRequestHeader: function(){}, getResponseHeader: function(){ return null; }, getAllResponseHeaders: function(){ return ''; }, abort: function(){}, addEventListener: function(t, f){ var k = 'on' + t; if (!this[k]) this[k] = f; } };\n"
"  g.crypto = g.crypto || (function(){ function C(){} C.prototype.getRandomValues = function(a){ var i; for (i = 0; i < a.length; i++) a[i] = (Math.random() * 256) | 0; return a; }; C.prototype.randomUUID = function(){ return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function(c){ var r = Math.random() * 16 | 0, v = c === 'x' ? r : (r & 0x3 | 0x8); return v.toString(16); }); }; return new C(); })();\n  g.performance = g.performance || { now: function(){ g.__nfuT0 = g.__nfuT0 || Date.now(); return Date.now() - g.__nfuT0; }, getEntries: function(){ return []; }, getEntriesByName: function(){ return []; }, getEntriesByType: function(){ return []; }, mark: function(){}, measure: function(){}, clearMarks: function(){}, clearMeasures: function(){} };\n"
"  // Guard the rest of the interface too: an existing-but-incomplete\n"
"  // performance object is what killed the Turnstile challenge (it calls\n"
"  // getEntries() while walking the timeline, got undefined, and threw a bare\n"
"  // \"not a function\" that surfaced only five seconds later).\n"
"  // performance.now() must be RELATIVE to page start. Returning Date.now()\n"
"  // (an absolute ~1.79e12 ms) makes every elapsed-time computation look like\n"
"  // an eternity, and bloxd's loader then declares the wasm download \"stalled\"\n"
"  // after 15 s of wall clock that never actually elapsed.\n"
"  try { g.__nfuT0 = g.__nfuT0 || Date.now(); g.performance.now = function(){ return Date.now() - g.__nfuT0; }; } catch(e){}\n"
"  if (typeof g.performance.getEntries !== 'function') g.performance.getEntries = function(){ return []; };\n"
"  if (typeof g.performance.clearResourceTimings !== 'function') g.performance.clearResourceTimings = function(){};\n"
"  if (typeof g.performance.setResourceTimingBufferSize !== 'function') g.performance.setResourceTimingBufferSize = function(){};\n"
"  if (typeof g.performance.timeOrigin === 'undefined')\n"
"    g.performance.timeOrigin = Date.now() - (typeof g.performance.now === 'function' ? g.performance.now() : 0);\n"
"  if (typeof g.performance.toJSON !== 'function')\n"
"    g.performance.toJSON = function(){ return { timeOrigin: g.performance.timeOrigin }; };\n"
"  // matchMedia must actually EVALUATE the query: bloxd picks its whole home\n"
"  // layout from (orientation: portrait) — with a hardcoded matches:false the\n"
"  // card carousels stay in their desktop column and stack 11k px tall each.\n"
"  g.matchMedia = function(q){\n"
"    var m = String(q || '').toLowerCase();\n"
"    var ow = g.innerWidth || 800, oh = g.innerHeight || 600;\n"
"    var ok = true, any = false, re = /\\(([^)]*)\\)/g, t;\n"
"    while ((t = re.exec(m))) {\n"
"      var c = t[1].replace(/\\s+/g, '');\n"
"      if (c.indexOf('orientation:portrait') >= 0) { any = true; if (!(oh > ow)) ok = false; }\n"
"      else if (c.indexOf('orientation:landscape') >= 0) { any = true; if (!(ow >= oh)) ok = false; }\n"
"      else if (c.indexOf('prefers-color-scheme:dark') >= 0) { any = true; ok = false; }\n"
"      else if (c.indexOf('prefers-color-scheme:light') >= 0) { any = true; }\n"
"      else if (c.indexOf('display-mode:') >= 0) { any = true; ok = false; }\n"
"      else if (c.indexOf('pointer:coarse') >= 0) { any = true; ok = false; }\n"
"      else if (c.indexOf('hover:hover') >= 0) { any = true; }\n"
"      else {\n"
"        var mm = /^(min|max)-(width|height):(\\d+)(px)?$/.exec(c);\n"
"        if (mm) {\n"
"          any = true; var v = (mm[2] === 'width') ? ow : oh, n = parseInt(mm[3], 10);\n"
"          if (mm[1] === 'min' && v < n) ok = false;\n"
"          if (mm[1] === 'max' && v > n) ok = false;\n"
"        }\n"
"      }\n"
"    }\n"
"    if (!any) ok = false;\n"
"    return { matches: ok, media: q, onchange: null,\n"
"             addListener: function(){}, removeListener: function(){},\n"
"             addEventListener: function(){}, removeEventListener: function(){} };\n"
"  };\n"
"  g.devicePixelRatio = 1;\n"
"  // Keep the viewport C++ already installed (it is the real layout viewport);\n"
"  // only fill in a fallback when nothing was provided.\n"
"  if (!g.innerWidth) g.innerWidth = 800;\n"
"  if (!g.innerHeight) g.innerHeight = 600;\n"
"  g.screen = { width: g.innerWidth, height: g.innerHeight, availWidth: g.innerWidth, availHeight: g.innerHeight, colorDepth: 24, pixelDepth: 24,\n"
"               orientation: { type: (g.innerHeight > g.innerWidth ? 'portrait-primary' : 'landscape-primary'), angle: 0 } };\n"
"  if (g.window) g.window.document = nd;\n"
"  // ---- modern browser APIs that SPA bundles expect at load time ----------\n"
"  function _Emitter(){ this._h = {}; }\n"
"  _Emitter.prototype.addEventListener = function(t, f){ (this._h[t] = this._h[t] || []).push(f); };\n"
"  _Emitter.prototype.removeEventListener = function(t, f){ var a = this._h[t]; if (a){ var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } };\n"
"  _Emitter.prototype._emit = function(t, ev){ var a = this._h[t]; if (!a) return; for (var i = 0; i < a.length; i++){ try { a[i].call(this, ev); } catch(e){} } };\n"
"  g.AbortSignal = function(){ _Emitter.call(this); this.aborted = false; this.reason = undefined; };\n"
"  g.AbortSignal.prototype = Object.create(_Emitter.prototype);\n"
"  g.AbortSignal.prototype.throwIfAborted = function(){ if (this.aborted) throw (this.reason || new Error('Aborted')); };\n"
"  g.AbortController = function(){ var s = new g.AbortSignal(); this.signal = s; this.abort = function(r){ if (s.aborted) return; try { print('[ABORT] reason=' + (r && r.message ? r.message : String(r)) + ' now=' + (g.performance && g.performance.now ? Math.round(g.performance.now()) : -1)); } catch(e){} s.aborted = true; s.reason = r || new Error('Aborted'); s._emit('abort', { type: 'abort', target: s }); }; };\n"
"  function _observer(entryFn){ return function(cb){ this._cb = cb; this._targets = []; this.observe = function(t){ this._targets.push(t); try { if (entryFn) entryFn(this, t); } catch(e){} }; this.unobserve = function(t){ var i = this._targets.indexOf(t); if (i >= 0) this._targets.splice(i, 1); }; this.disconnect = function(){ this._targets.length = 0; }; this.takeRecords = function(){ return []; }; }; }\n"
"  // ---- indexedDB / Worker stubs -----------------------------------------\n"
"  // React SPAs open indexedDB during boot (storage of session/state). With\n"
"  // the global MISSING the async init chain throws, catches and retries —\n"
"  // an allocation loop that ran the heap from 15MB to 1.4GB in 12s\n"
"  // (chat.deepseek.com). A stub whose requests fail ASYNC and exactly once\n"
"  // lets the app take its no-storage fallback path and keep booting.\n"
"  if (typeof g.indexedDB === 'undefined'){\n"
"    function _idbLs(self){ return self._h || (self._h = {}); }\n"
"    function _idbFire(self, type){ var ev = { type: type, target: self };\n"
"      var h = self['on' + type]; if (h) try { h.call(self, ev); } catch(e){}\n"
"      var ls = _idbLs(self)[type]; if (ls) for (var i = 0; i < ls.length; i++) try { ls[i].call(self, ev); } catch(e2){} }\n"
"    function _idbFail(self){ (g.__nfuTimers = g.__nfuTimers || []).push(function(){ self.readyState = 'done'; self.error = new DOMException('The operation failed', 'UnknownError'); _idbFire(self, 'error'); }); }\n"
"    function _idbOk(self, result){ (g.__nfuTimers = g.__nfuTimers || []).push(function(){ self.readyState = 'done'; self.result = result; _idbFire(self, 'success'); }); }\n"
"    function _idbReq(){ this.readyState = 'pending'; this.result = null; this.error = null; this.onsuccess = null; this.onerror = null; this.onupgradeneeded = null; this.oncomplete = null; this._h = {}; this.addEventListener = function(t, f){ var ls = _idbLs(this); (ls[String(t)] = ls[String(t)] || []).push(f); }; this.removeEventListener = function(t, f){ var ls = _idbLs(this)[String(t)]; if (ls){ var i = ls.indexOf(f); if (i >= 0) ls.splice(i, 1); } }; }\n"
"    g.IDBRequest = _idbReq; g.IDBOpenDBRequest = _idbReq; g.IDBVersionChangeEvent = function(t){ this.type = String(t || 'versionchange'); };\n"
"    g.IDBKeyRange = { bound: function(a,b){ return {lower:a,upper:b}; }, only: function(a){ return {lower:a,upper:a}; }, lowerBound: function(a){ return {lower:a}; }, upperBound: function(a){ return {upper:a}; } };\n"
"    // A failing open() is fine for storage-optional SPAs, but bloxd caches its\n"
"    // texture packs here and awaits the result before loading its 3D libraries:\n"
"    // every request failing left the game stuck on 'Loading required libraries'\n"
"    // with a 0% bar. Back the stub with a real in-memory key/value store that\n"
"    // answers success (null on miss) the way a fresh browser profile would.\n"
"    var _idbNames = (g.__nfuIdb = g.__nfuIdb || { name: {}, ver: {}, seq: 0 });\n"
"    function _idbStore(name){\n"
"      var o = { name: name, keyPath: null, autoIncrement: false };\n"
"      var m = _idbNames.name[name] = _idbNames.name[name] || {};\n"
"      // A miss must be undefined, not null: IDBObjectStore.get() gives undefined\n"
"      // per spec, and firebase's wrapper tests `void 0 === v ? null : v.value`,\n"
"      // so a null result threw 'cannot read property value of null'. That\n"
"      // rejection killed the Promise.all behind 'Loading required libraries',\n"
"      // leaving the loading bar at 0% with no visible error at all.\n"
"      o.get = function(k){ var r = new _idbReq(); var hit = (typeof k === 'string' && m.hasOwnProperty(k)); _idbOk(r, hit ? m[k] : void 0); return r; };\n"
"      o.put = function(v, k){ var key = (k === undefined) ? ('k' + (++_idbNames.seq)) : k; m[key] = v; var r = new _idbReq(); _idbOk(r, key); return r; };\n"
"      o.add = o.put;\n"
"      o.delete = function(k){ try { delete m[k]; } catch(e){} var r = new _idbReq(); _idbOk(r, undefined); return r; };\n"
"      o.clear = function(){ var self = m; for (var k in self) if (self.hasOwnProperty(k)) delete self[k]; var r = new _idbReq(); _idbOk(r, undefined); return r; };\n"
"      o.getAll = function(){ var a = [], k; for (k in m) if (m.hasOwnProperty(k)) a.push(m[k]); var r = new _idbReq(); _idbOk(r, a); return r; };\n"
"      o.getAllKeys = function(){ var a = [], k; for (k in m) if (m.hasOwnProperty(k)) a.push(k); var r = new _idbReq(); _idbOk(r, a); return r; };\n"
"      o.count = function(){ var n = 0, k; for (k in m) if (m.hasOwnProperty(k)) n++; var r = new _idbReq(); _idbOk(r, n); return r; };\n"
"      o.openCursor = function(){ var r = new _idbReq(); _idbOk(r, null); return r; };\n"
"      o.createIndex = function(){ return { count: function(){ var r = new _idbReq(); _idbOk(r, 0); return r; } }; };\n"
"      o.index = o.createIndex;\n"
"      return o;\n"
"    }\n"
"    function _idbTx(dbName){\n"
"      var self = this;\n"
"      this.dbName = dbName; this.oncomplete = null; this.onerror = null; this.onabort = null; this._h = {};\n"
"      this.addEventListener = function(t, f){ (_idbLs(this)[String(t)] = _idbLs(this)[String(t)] || []).push(f); };\n"
"      this.removeEventListener = function(t, f){ var a = _idbLs(this)[String(t)]; if (a){ var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } };\n"
"      this.objectStore = function(n){ return _idbStore(dbName + '/' + n); };\n"
"      this.abort = function(){}; this.mode = 'readwrite';\n"
"      (g.__nfuTimers = g.__nfuTimers || []).push(function(){ if (self.aborted) return; var ev = { type: 'complete', target: self };\n"
"        if (self.oncomplete) try { self.oncomplete(ev); } catch(e){}\n"
"        var ls = _idbLs(self)['complete']; if (ls) for (var i = 0; i < ls.length; i++) try { ls[i].call(self, ev); } catch(e2){} });\n"
"    }\n"
"    function _idbDb(name, ver){\n"
"      var self = this;\n"
"      this.name = String(name); this.version = ver || 1;\n"
"      this.objectStoreNames = { contains: function(n){ return true; }, length: 1, item: function(){ return null; } };\n"
"      this.createObjectStore = function(n){ return _idbStore(self.name + '/' + n); };\n"
"      this.transaction = function(){ return new _idbTx(self.name); };\n"
"      this.close = function(){}; this.onversionchange = null;\n"
"      this.addEventListener = function(){}; this.removeEventListener = function(){};\n"
"    }\n"
"    g.indexedDB = {\n"
"      open: function(name, ver){\n"
"        var r = new _idbReq(); r.name = String(name);\n"
"        var isNew = !(String(name) in _idbNames.ver);\n"
"        if (isNew) try { print('[IDB] create ' + r.name); } catch(e){}\n"
"        var db = new _idbDb(name, ver || (_idbNames.ver[name] || 1));\n"
"        _idbNames.ver[name] = db.version;\n"
"        if (isNew){\n"
"          // Wrappers that only create their object stores inside\n"
"          // onupgradeneeded never see them when we jump straight to success.\n"
"          // Report a version bump first, then success, in that order.\n"
"          r.result = db;\n"
"          var evUp = { type: 'upgradeneeded', target: r, oldVersion: 0, newVersion: db.version };\n"
"          (g.__nfuTimers = g.__nfuTimers || []).push(function(){\n"
"            var h = r.onupgradeneeded; if (h) try { h.call(r, evUp); } catch(e){}\n"
"            var ls = _idbLs(r)['upgradeneeded']; if (ls) for (var i = 0; i < ls.length; i++) try { ls[i].call(r, evUp); } catch(e2){}\n"
"          });\n"
"        }\n"
"        _idbOk(r, db);\n"
"        return r;\n"
"      },\n"
"      deleteDatabase: function(name){ var r = new _idbReq(); _idbNames.name[name] = {}; _idbOk(r, undefined); return r; },\n"
"      cmp: function(a, b){ return a < b ? -1 : (a > b ? 1 : 0); },\n"
"      databases: function(){ return Promise.resolve([]); }\n"
"    };\n"
"  }\n"
"  // ---- Web Worker: real in-context execution ---------------------------\n"
"  // bloxd builds every worker from a Blob, and URL.createObjectURL turns that\n"
"  // blob into a base64 data: URL here, so the worker source is already in\n"
"  // memory. Workers therefore run inside this context, interleaved on the\n"
"  // timer queue: no OS thread and no second QuickJS context. postMessage deep\n"
"  // clones its payload so the two sides cannot alias each other's objects.\n"
"  function _wcClone(v){\n"
"    if (v === null || typeof v !== 'object') return v;\n"
"    // wasm objects cannot be deep-copied: WebAssembly.Module/Instance/Memory\n"
"    // are opaque handles whose own enumerable keys are empty, so the generic\n"
"    // object branch below would hand the worker a useless {} and the worker\n"
"    // would then fail with \"argument is not a BufferSource\". Both sides run in\n"
"    // the same context here, so pass the handle straight through.\n"
"    try { if (typeof WebAssembly !== 'undefined'){ if (v instanceof WebAssembly.Module || v instanceof WebAssembly.Instance || v instanceof WebAssembly.Memory) return v; } } catch(eW){}\n"
"    // MessagePort must travel by reference, not by value. bloxd transfers one\n"
"    // to the world-gen worker (`postMessage({type:'attachRoomPort',port}, [port])`)\n"
"    // and the port graph is cyclic (port1._peer === port2, port2._peer === port1),\n"
"    // so the generic object branch below recursed until the JS stack blew up and\n"
"    // aborted the whole attach step: the worker never received a room port, so it\n"
"    // never produced a seed or a chunk. Both contexts share one VM here.\n"
"    if (v._isNfuPort) return v;\n"
"    if (v instanceof ArrayBuffer){ var ab = new ArrayBuffer(v.byteLength); new Uint8Array(ab).set(new Uint8Array(v)); return ab; }\n"
"    if (typeof v.length === 'number' && v.buffer && v.buffer instanceof ArrayBuffer && typeof v.byteLength === 'number'){\n"
"      try { var ta = new v.constructor(v.length); ta.set(v); return ta; } catch(e0){}\n"
"    }\n"
"    if (v instanceof Date) return new Date(v.getTime());\n"
"    var i, k, keys, out;\n"
"    if (v instanceof Array){ out = []; for (i = 0; i < v.length; i++) out.push(_wcClone(v[i])); return out; }\n"
"    out = {}; keys = Object.keys(v);\n"
"    for (i = 0; i < keys.length; i++){ k = keys[i]; try { out[k] = _wcClone(v[k]); } catch(e){ out[k] = null; } }\n"
"    return out;\n"
"  }\n"
"  function _wSrc(url){\n"
"    url = String(url);\n"
"    if (url.indexOf('data:') === 0){\n"
"      var ci = url.indexOf(';base64,');\n"
"      if (ci >= 0){ try { return _utf8dec(_b64dec(url.slice(ci + 8))); } catch(e){ return ''; } }\n"
"      try { return decodeURIComponent(url.slice(url.indexOf(',') + 1)); } catch(e){ return ''; }\n"
"    }\n"
"    return '';\n"
"  }\n"
"  function _wNow(){ try { return g.performance && g.performance.now ? g.performance.now() : Date.now(); } catch(e){ return Date.now(); } }\n"
"  // WebAssembly failures inside a worker are invisible from the main thread:\n"
"  // the worker keeps running, the page keeps clearing, and both sides wait on\n"
"  // each other. Report instantiation outcome so a failed engine build shows up.\n"
"  try {\n"
"    if (typeof WebAssembly !== 'undefined' && !WebAssembly.__nfuProbe) {\n"
"      WebAssembly.__nfuProbe = 1;\n"
"      var _nfuInst = WebAssembly.instantiate;\n"
"      WebAssembly.instantiate = function(a, b) {\n"
"        var p = _nfuInst.apply(WebAssembly, arguments);\n"
"        try {\n"
"          p.then(function(r){ try { print('[WASM-INS-OK] ' + (r && r.instance ? 'obj' : 'buf')); } catch(e){}\n"
"                 }, function(e){ try { print('[WASM-INS-REJ] ' + String(e && e.message ? e.message : e).slice(0, 140)); } catch(e2){} });\n"
"        } catch(e3){}\n"
"        return p;\n"
"      };\n"
"    }\n"
"  } catch(eW2){}\n"
"  if (typeof g.Worker === 'undefined' || !g.Worker.__nfuReal){\n"
"    g.Worker = function(url, opts){\n"
"      var me = this;\n"
"      this.onmessage = null; this.onerror = null; this.onmessageerror = null;\n"
"      this._h = {}; this._dead = 0; this._scope = null;\n"
"      this.addEventListener = function(t, f){ (me._h[t] = me._h[t] || []).push(f); };\n"
"      this.removeEventListener = function(t, f){ var a = me._h[t]; if (a){ var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } };\n"
"      var toMain = function(m){\n"
"        if (me._dead) return;\n"
"        // Worker -> main traffic matters as much as the other direction: chunk data\n"
"        // comes back through here, so a silent worker and a worker that never\n"
"        // answers used to look identical.\n"
"        try {\n"
"          g.__nfuWrN = (g.__nfuWrN || 0) + 1;\n"
"          if (g.__nfuWrN <= 12) {\n"
"            var _rks = (m && typeof m === 'object') ? Object.keys(m).slice(0, 8).join(',') : typeof m;\n"
"            print('[WRECV] n=' + g.__nfuWrN + ' type=' + String(m && m.type ? m.type : '-') + ' keys=' + _rks + ' val=' + String(m).slice(0, 90));\n"
"          }\n"
"        } catch(_re){}\n"
"        var c = _wcClone(m);\n"
"        (g.__nfuWq = g.__nfuWq || []).push(function(){\n"
"          if (me._dead) return;\n"
"          var ev = { type: 'message', data: _wcClone(c), target: me, currentTarget: me };\n"
"          try { if (me.onmessage) me.onmessage(ev); } catch(e){ try { print('[WORKER-MSGERR] ' + (e && e.message ? e.message : e)); } catch(pe){} }\n"
"          var hs = (me._h.message || []).slice();\n"
"          for (var i = 0; i < hs.length; i++){ try { hs[i](ev); } catch(e2){} }\n"
"        });\n"
"      };\n"
"      this.postMessage = function(m, transfer){\n"
"        // transfer is not optional in practice: worker pools hand their port over\n"
"        // as postMessage(init, [port]) and drop the whole channel when the second\n"
"        // argument is ignored. Nothing real is transferred here (one JS context),\n"
"        // but the ports must survive the clone so the worker can reply.\n"
"        try {\n"
"          var _mk = '(none)';\n"
"          try { if (m && typeof m === 'object') _mk = Object.keys(m).slice(0, 8).join(',') || '(empty)'; else _mk = typeof m; } catch(_ke){}\n"
"          print('[WPOST] type=' + String(m && m.type ? m.type : '-') + ' keys=' + _mk + ' xfer=' + (transfer ? (transfer.length || 0) : 0) + ' q=' + ((g.__nfuTimers || []).length) + ' sg=' + (g === globalThis ? 1 : 0) + ' same=' + (g.__nfuTimers === globalThis.__nfuTimers ? 1 : 0));\n"
"        } catch(pe){}\n"
"        var c = _wcClone(m);\n"
"        var xf = [];\n"
"        try { if (transfer && transfer.length) for (var ti = 0; ti < transfer.length; ti++) { var tv = _wcClone(transfer[ti]); if (tv && tv._isNfuPort) xf.push(tv); } } catch(te){}\n"
"        (g.__nfuWq = g.__nfuWq || []).push(function(){\n"
"          try {\n"
"            g.__nfuWdN = (g.__nfuWdN || 0) + 1;\n"
"            if (g.__nfuWdN <= 6) {\n"
"              var _wsc = me._scope;\n"
"              print('[WDELIV] n=' + g.__nfuWdN + ' dead=' + (me._dead ? 1 : 0) + ' hasOn=' + (_wsc && _wsc.onmessage ? 1 : 0) + ' h=' + (_wsc && _wsc._h && _wsc._h.message ? _wsc._h.message.length : 0));\n"
"            }\n"
"          } catch(_wd){}\n"
"          if (me._dead || !me._scope) { try { print('[WDROP] dead=1'); } catch(_d){} return; }\n"
"          var ev = { type: 'message', data: _wcClone(c), ports: xf, target: me._scope };\n"
"          try { if (me._scope.onmessage) me._scope.onmessage(ev);\n"
"                else { var a = me._scope._h.message || []; for (var i = 0; i < a.length; i++) a[i](ev); } }\n"
"          catch(e){\n"
"            try { print('[WORKER-ERR] ' + (e && e.message ? e.message : e)); } catch(pe){}\n"
"            var ev2 = { type: 'error', message: String(e && e.message ? e.message : e), target: me };\n"
"            if (me.onerror) try { me.onerror(ev2); } catch(pe){}\n"
"          }\n"
"        });\n"
"      };\n"
"      this.terminate = function(){ me._dead = 1; };\n"
"      var src = _wSrc(url);\n"
"      if (!src && (/^blob:|^http/.test(String(url)))) { try { src = String(__nfuHttpGet(String(url))); } catch(e3){ src = ''; } }\n"
"      if (!src){\n"
"        (g.__nfuWq = g.__nfuWq || []).push(function(){\n"
"          var ev = { type: 'error', message: 'Workers are not supported', target: me };\n"
"          if (me.onerror) try { me.onerror(ev); } catch(e){}\n"
"          var hs = (me._h.error || []).slice(); for (var i = 0; i < hs.length; i++) try { hs[i](ev); } catch(e){}\n"
"        });\n"
"        return;\n"
"      }\n"
"      var sc = { onmessage: null, onerror: null, onmessageerror: null, _h: {}, name: '',\n"
"                 postMessage: toMain, self: null, window: null, globalThis: null,\n"
"                 close: function(){ me._dead = 1; },\n"
"                 // importScripts used to be a no-op, so a worker that bootstraps\n"
"                 // itself from a sibling file stayed half-born. Babylon's Draco decoder\n"
"                 // does exactly that: on {\"init\"} it runs importScripts(glueUrl) and then\n"
"                 // calls DracoDecoderModule({wasmBinary}). With nothing imported that\n"
"                 // global was undefined and all compressed models failed with 'Draco:\n"
"                 // DracoDecoderModule is not defined'. Indirect eval keeps the script in\n"
"                 // global scope, which is where the worker's free identifiers look.\n"
"                 importScripts: function(){\n"
"                   for (var ii = 0; ii < arguments.length; ii++){\n"
"                     var iu = String(arguments[ii]); var itxt = '';\n"
"                     try { itxt = String(__nfuHttpGet(iu)); } catch(ie){ try { print('[WSCRIPT-FETCH-ERR] ' + iu.slice(0, 90)); } catch(pe){} }\n"
"                     if (itxt) {\n"
"                       try { (0, eval)(itxt); try { print('[WSCRIPT-OK] ' + iu.slice(0, 80) + ' ' + itxt.length); } catch(pe){} }\n"
"                       catch(ee){ try { print('[WSCRIPT-ERR] ' + iu.slice(0, 80) + ' :: ' + (ee && ee.message ? ee.message : ee)); } catch(pe){} }\n"
"                     }\n"
"                   }\n"
"                 },\n"
"                 location: g.location, navigator: g.navigator,\n"
"                 addEventListener: function(t, f){ (sc._h[t] = sc._h[t] || []).push(f); },\n"
"                 removeEventListener: function(t, f){ var a = sc._h[t]; if (a){ var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } } };\n"
"      sc.self = sc; sc.window = sc; sc.globalThis = sc;\n"
"      me._scope = sc;\n"
"      (g.__nfuWq = g.__nfuWq || []).push(function(){\n"
"        if (me._dead) return;\n"
"        try {\n"
"          var f = new Function('self', 'postMessage', 'addEventListener', 'removeEventListener',\n"
"                              'importScripts', 'close', 'onmessage', 'location', 'navigator',\n"
"                              'setTimeout', 'clearTimeout', 'setInterval', 'clearInterval',\n"
"                              'performance', 'console', 'globalThis', src);\n"
"          f.call(sc, sc, toMain, sc.addEventListener, sc.removeEventListener,\n"
"                 sc.importScripts, sc.close, null, sc.location, sc.navigator,\n"
"                 g.setTimeout, g.clearTimeout, g.setInterval, g.clearInterval,\n"
"                 g.performance, g.console, sc);\n"
"          try { print('[WORKER-OK] ' + String(url).slice(0, 60) + ' ' + src.length); } catch(pe){}\n"
"        } catch(e){\n"
"          try { print('[WORKER-LOADERR] ' + String(url).slice(0, 70) + ' :: ' + (e && e.message ? e.message : e)); } catch(pe){}\n"
"          var ev2 = { type: 'error', message: String(e && e.message ? e.message : e), target: me };\n"
"          if (me.onerror) try { me.onerror(ev2); } catch(pe){}\n"
"          var hs2 = (me._h.error || []).slice(); for (var i2 = 0; i2 < hs2.length; i2++) try { hs2[i2](ev2); } catch(pe){}\n"
"        }\n"
"      });\n"
"    };\n"
"    g.Worker.__nfuReal = 1;\n"
"  }\n"
"  // ---- EventTarget ------------------------------------------------------\n"
"  // Bundles routinely subclass it (\"class Bus extends EventTarget\") or patch\n"
"  // EventTarget.prototype.addEventListener with a passive-by-default wrapper.\n"
"  // Leaving it undefined threw \"cannot read property 'prototype' of\n"
"  // undefined\" at module-eval time and killed the whole app bundle\n"
"  // (chat.deepseek.com never rendered a single node).\n"
"  g.EventTarget = function(){ this._h = {}; };\n"
"  g.EventTarget.prototype.addEventListener = function(t, f){ if (!f) return; var a = this._h[t] || (this._h[t] = []); if (a.indexOf(f) < 0) a.push(f); };\n"
"  g.EventTarget.prototype.removeEventListener = function(t, f){ var a = this._h && this._h[t]; if (!a) return; var i = a.indexOf(f); if (i >= 0) a.splice(i, 1); };\n"
"  g.EventTarget.prototype.dispatchEvent = function(ev){ ev = ev || {}; var t = ev.type, a = this._h && this._h[t]; if (a){ for (var i = 0; i < a.length; i++){ try { a[i].call(this, ev); } catch(e){} } } try { if (this['on' + t]) this['on' + t](ev); } catch(e){} return true; };\n"
"  // ---- TextEncoder / TextDecoder ---------------------------------------\n"
"  // fetch().text()/JSON bodies and every crypto helper bundle goes through\n"
"  // these; a missing TextEncoder aborted chat.deepseek.com's boot.\n"
"  function _utf8enc(s){\n"
"    s = String(s);\n"
"    if (typeof __nfuUtf8Enc === 'function') return new Uint8Array(__nfuUtf8Enc(s));\n"
"    var o = [], i, c;\n"
"    for (i = 0; i < s.length; i++){\n"
"      c = s.charCodeAt(i);\n"
"      if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.length){\n"
"        var c2 = s.charCodeAt(i + 1);\n"
"        if (c2 >= 0xDC00 && c2 <= 0xDFFF){ c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00); i++; }\n"
"      }\n"
"      if (c < 0x80) o.push(c);\n"
"      else if (c < 0x800) o.push(0xC0 | (c >> 6), 0x80 | (c & 63));\n"
"      else if (c < 0x10000) o.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));\n"
"      else o.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));\n"
"    }\n"
"    return new Uint8Array(o);\n"
"  }\n"
"  function _utf8dec(b){\n"
"    if (b == null) return '';\n"
"    if (typeof __nfuUtf8Dec === 'function'){ try { return __nfuUtf8Dec(b); } catch(eD){} }\n"
"    if (b.buffer) b = new Uint8Array(b.buffer, b.byteOffset || 0, b.byteLength || b.length);\n"
"    var o = '', i = 0, n = b.length;\n"
"    while (i < n){\n"
"      var c = b[i++];\n"
"      if (c < 0x80) o += String.fromCharCode(c);\n"
"      else if ((c & 0xE0) === 0xC0 && i < n) o += String.fromCharCode(((c & 31) << 6) | (b[i++] & 63));\n"
"      else if ((c & 0xF0) === 0xE0 && i + 1 < n) o += String.fromCharCode(((c & 15) << 12) | ((b[i++] & 63) << 6) | (b[i++] & 63));\n"
"      else if ((c & 0xF8) === 0xF0 && i + 2 < n){\n"
"        var cp = ((c & 7) << 18) | ((b[i++] & 63) << 12) | ((b[i++] & 63) << 6) | (b[i++] & 63);\n"
"        cp -= 0x10000; o += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 1023));\n"
"      } else o += '\\uFFFD';\n"
"    }\n"
"    return o;\n"
"  }\n"
"  if (typeof g.TextEncoder === 'undefined'){\n"
"    g.TextEncoder = function(){ this.encoding = 'utf-8'; };\n"
"    g.TextEncoder.prototype.encode = function(s){ return _utf8enc(s === undefined ? '' : s); };\n"
"    g.TextEncoder.prototype.encodeInto = function(s, dst){ var u = _utf8enc(s), i; for (i = 0; i < u.length && i < dst.length; i++) dst[i] = u[i]; return { read: String(s).length, written: i }; };\n"
"    g.TextDecoder = function(l){ this.encoding = l || 'utf-8'; this.fatal = false; this.ignoreBOM = false; };\n"
"    g.TextDecoder.prototype.decode = function(b){ return _utf8dec(b); };\n"
"  }\n"
"  // ---- Intl -------------------------------------------------------------\n"
"  // QuickJS ships without ICU, so Intl is absent. Cloudflare's Turnstile\n"
"  // challenge reads locale dates/numbers during its bootstrap and dies with\n"
"  // 'not a function (got undefined)' when Intl.* is missing - that error was\n"
"  // the visible symptom, not a DOM bug.\n"
"  if (typeof g.Intl === 'undefined'){\n"
"    var _pad2 = function(n){ return (n < 10 ? '0' : '') + n; };\n"
"    function _dfParts(locale, opts){\n"
"      opts = opts || {};\n"
"      var zh = String(locale || 'en').toLowerCase().indexOf('zh') === 0;\n"
"      return function(d){\n"
"        d = d === undefined ? new Date() : (d instanceof Date ? d : new Date(d));\n"
"        var Y = d.getFullYear(), M = d.getMonth() + 1, D = d.getDate();\n"
"        var h = d.getHours(), m = d.getMinutes(), s = d.getSeconds();\n"
"        var out = [], sep = zh ? '/' : '/';\n"
"        if (opts.year || opts.month || opts.day || (!opts.hour && !opts.minute)){\n"
"          if (zh) out.push(String(Y), _pad2(M), _pad2(D));\n"
"          else out.push(String(M), _pad2(D), String(Y));\n"
"        }\n"
"        if (opts.hour || opts.minute || opts.second) out.push(_pad2(h) + ':' + _pad2(m) + ':' + _pad2(s));\n"
"        return out.join(zh ? sep : (out.length > 1 && out[0].length <= 2 ? sep : ' '));\n"
"      };\n"
"    }\n"
"    var Intl = {};\n"
"    // Intl formatters are callable with or without 'new' in every real\n"
"    // browser. A constructor that only supports 'new' returns undefined for\n"
"    // 'Intl.DateTimeFormat(...)' and the next '.resolvedOptions' throws - that\n"
"    // single call aborted bloxd's whole main bundle.\n"
"    Intl.DateTimeFormat = function(locale, opts){\n"
"      if (!(this instanceof Intl.DateTimeFormat)) return new Intl.DateTimeFormat(locale, opts);\n"
"      this._f = _dfParts(locale, opts);\n"
"      this._l = String(locale || 'en-US');\n"
"      this._o = opts || {};\n"
"    };\n"
"    Intl.DateTimeFormat.prototype.format = function(d){ return this._f(d); };\n"
"    Intl.DateTimeFormat.prototype.formatToParts = function(d){ return [{ type: 'literal', value: this._f(d) }]; };\n"
"    Intl.DateTimeFormat.prototype.resolvedOptions = function(){\n"
"      return { locale: this._l, calendar: 'gregory', numberingSystem: 'latn', timeZone: 'UTC', hour12: false };\n"
"    };\n"
"    Intl.DateTimeFormat.supportedLocalesOf = function(l){ return l || []; };\n"
"    Intl.NumberFormat = function(locale, opts){\n"
"      if (!(this instanceof Intl.NumberFormat)) return new Intl.NumberFormat(locale, opts);\n"
"      this._o = opts || {};\n"
"      this._l = String(locale || 'en-US');\n"
"    };\n"
"    Intl.NumberFormat.prototype.format = function(n){\n"
"      var o = this._o; n = +n;\n"
"      if (isNaN(n)) return 'NaN';\n"
"      if (o.style === 'percent') return (n * 100).toFixed(o.maximumFractionDigits === undefined ? 0 : o.maximumFractionDigits) + '%';\n"
"      var fd = o.minimumFractionDigits !== undefined ? o.minimumFractionDigits : (o.maximumFractionDigits || 0);\n"
"      var s = n.toFixed(fd), sign = '';\n"
"      if (s.charAt(0) === '-'){ sign = '-'; s = s.substring(1); }\n"
"      if (o.useGrouping === false) return sign + s;\n"
"      var dot = s.indexOf('.'), ip = dot < 0 ? s : s.substring(0, dot), fp = dot < 0 ? '' : s.substring(dot);\n"
"      var g = '', c = 0, i;\n"
"      for (i = ip.length - 1; i >= 0; i--){ g = ip.charAt(i) + g; if (++c % 3 === 0 && i > 0) g = ',' + g; }\n"
"      return sign + g + fp;\n"
"    };\n"
"    Intl.NumberFormat.prototype.resolvedOptions = function(){ return { locale: this._l, numberingSystem: 'latn' }; };\n"
"    Intl.NumberFormat.supportedLocalesOf = function(l){ return l || []; };\n"
"    Intl.Collator = function(locale, opts){\n"
"      if (!(this instanceof Intl.Collator)) return new Intl.Collator(locale, opts);\n"
"      this._o = opts || {};\n"
"    };\n"
"    Intl.Collator.prototype.compare = function(a, b){ a = String(a); b = String(b); if (this._o.numeric) { var na = parseFloat(a), nb = parseFloat(b); if (!isNaN(na) && !isNaN(nb) && na !== nb) return na < nb ? -1 : 1; } return a < b ? -1 : (a > b ? 1 : 0); };\n"
"    Intl.Collator.prototype.resolvedOptions = function(){ return { locale: 'en-US', usage: 'sort' }; };\n"
"    Intl.PluralRules = function(locale, opts){ if (!(this instanceof Intl.PluralRules)) return new Intl.PluralRules(locale, opts); };\n"
"    Intl.PluralRules.prototype.select = function(n){ return n === 1 ? 'one' : 'other'; };\n"
"    Intl.PluralRules.prototype.resolvedOptions = function(){ return { locale: 'en-US' }; };\n"
"    Intl.getCanonicalLocales = function(l){ return l ? [].concat(l) : []; };\n"
"    g.Intl = Intl;\n"
"  }\n"
"  // ---- crypto.subtle ----------------------------------------------------\n"
"  // sha256 in JS: the challenge platform hashes collected signals before it\n"
"  // posts them, and a missing subtle.digest throws inside its promise chain.\n"
"  if (g.crypto && !g.crypto.subtle){\n"
"    function _sha256(bytes){\n"
"      var K = [1116352408,1899447441,3049323471,3921009573,961987163,1508970993,2453635748,2870763221,3624381080,310598401,607225278,1426881987,1925078388,2162078206,2614888103,3248222580,3835390401,4022224774,264347078,604807628,770255983,1249150122,1555081692,1996064986,2554220882,2821834349,2952996808,3210313671,3336571891,3584528711,113926993,338241895,666307205,773529912,1294757372,1396182291,1695183700,1986661051,2177026350,2456956037,2730485921,2820302411,3259730800,3345764771,3516065817,3600352804,4094571909,275423344,430227734,506948616,659060556,883997877,958139571,1322822218,1537002063,1747873779,1955562222,2024104815,2227730452,2361852424,2428436474,2756734187,3204031479,3329325298];\n"
"      var H = [1779033703,3144134277,1013904242,2773480762,1359893119,2600822924,528734635,1541459225];\n"
"      var L = bytes.length, bl = L + 9, tot = ((bl + 63) >> 6) << 6;\n"
"      var buf = new Uint8Array(tot); buf.set(bytes); buf[L] = 0x80;\n"
"      var bitLen = L * 8;\n"
"      for (var q = 0; q < 4; q++) buf[tot - 1 - q] = (bitLen >>> (8 * q)) & 0xff;\n"
"      var w = new Array(64), i, t, a, b, c, d, e, f, gg, h, w0, w1, w15, w16;\n"
"      for (var off = 0; off < tot; off += 64){\n"
"        for (i = 0; i < 16; i++) w[i] = (buf[off+i*4]<<24) | (buf[off+i*4+1]<<16) | (buf[off+i*4+2]<<8) | buf[off+i*4+3];\n"
"        for (i = 16; i < 64; i++){\n"
"          w0 = w[i-15]; w1 = w[i-2]; w15 = w[i-16]; w16 = w[i-7];\n"
"          var s0 = ((w0>>>7)|(w0<<25)) ^ ((w0>>>18)|(w0<<14)) ^ (w0>>>3);\n"
"          var s1 = ((w1>>>17)|(w1<<15)) ^ ((w1>>>19)|(w1<<13)) ^ (w1>>>10);\n"
"          w[i] = (w15 + s0 + w16 + s1) | 0;\n"
"        }\n"
"        a=H[0]; b=H[1]; c=H[2]; d=H[3]; e=H[4]; f=H[5]; gg=H[6]; h=H[7];\n"
"        for (i = 0; i < 64; i++){\n"
"          var S1 = ((e>>>6)|(e<<26)) ^ ((e>>>11)|(e<<21)) ^ ((e>>>25)|(e<<7));\n"
"          var ch = (e & f) ^ ((~e) & gg);\n"
"          t = (h + S1 + ch + K[i] + w[i]) | 0;\n"
"          var S0 = ((a>>>2)|(a<<30)) ^ ((a>>>13)|(a<<19)) ^ ((a>>>22)|(a<<10));\n"
"          var mj = (a & b) ^ (a & c) ^ (b & c);\n"
"          h = gg; gg = f; f = e; e = (d + t) | 0; d = c; c = b; b = a; a = (t + S0 + mj) | 0;\n"
"        }\n"
"        H[0] = (H[0] + a) | 0; H[1] = (H[1] + b) | 0; H[2] = (H[2] + c) | 0; H[3] = (H[3] + d) | 0;\n"
"        H[4] = (H[4] + e) | 0; H[5] = (H[5] + f) | 0; H[6] = (H[6] + gg) | 0; H[7] = (H[7] + h) | 0;\n"
"      }\n"
"      var out = new Uint8Array(32), p = 0;\n"
"      for (i = 0; i < 8; i++){ out[p++] = (H[i]>>>24)&0xff; out[p++] = (H[i]>>>16)&0xff; out[p++] = (H[i]>>>8)&0xff; out[p++] = H[i]&0xff; }\n"
"      return out.buffer;\n"
"    }\n"
"    function _toBytes(d){\n"
"      if (d instanceof ArrayBuffer) return new Uint8Array(d);\n"
"      if (d && d.buffer instanceof ArrayBuffer) return new Uint8Array(d.buffer, d.byteOffset || 0, d.byteLength === undefined ? d.length : d.byteLength);\n"
"      if (typeof d === 'string') return _utf8enc(d);\n"
"      return _utf8enc(String(d));\n"
"    }\n"
"    g.crypto.subtle = {\n"
"      digest: function(alg, data){\n"
"        return new Promise(function(res, rej){\n"
"          try {\n"
"            var a = String(alg).toUpperCase().replace(/-/g, '');\n"
"            try { print('[DIGEST] alg=' + a + ' data=' + (typeof data) + ' len=' + (data && typeof data.byteLength === 'number' ? data.byteLength : (data && data.length !== undefined ? data.length : -1))); } catch(pe){}\n"
"            if (typeof __nfuSha256 === 'function' && a === 'SHA256') {\n"
"              var _d = _toBytes(data);\n"
"              var _off = _d.byteOffset || 0;\n"
"              var _ab = (_off || _d.byteLength !== _d.buffer.byteLength) ? _d.buffer.slice(_off, _off + _d.byteLength) : _d.buffer;\n"
"              res(__nfuSha256(_ab));\n"
"            } else { res(_sha256(_toBytes(data))); }\n"
"          } catch(e){ try { print('[DIGEST-ERR] ' + (e && e.message ? e.message : e)); } catch(pe){} rej(e); }\n"
"        });\n"
"      }\n"
"    };\n"
"    // Probe: bloxd's transport may reach for signatures, key import or AES\n"
"    // through SubtleCrypto. Anything missing here stays undefined and the join\n"
"    // payload would go out unsigned, which the server silently ignores.\n"
"    try {\n"
"      if (typeof Proxy === 'function'){\n"
"        var _subReal = g.crypto.subtle;\n"
"        g.crypto.subtle = new Proxy(_subReal, { get: function(t, k){\n"
"          if (typeof k === 'string'){ try { print('[SUBTLE] ' + ((k in t) ? 'get ' : 'MISSING ') + k); } catch(pe){} }\n"
"          return t[k];\n"
"        } });\n"
"      }\n"
"    } catch(ep){ try { print('[SUBTLE] proxy-failed ' + ep); } catch(pe2){} }\n"
"  }\n"
"  // createImageBitmap / Audio: fingerprint paths only. Real decoding is not\n"
"  // needed; what matters is that the calls exist and do not throw.\n"
"  // createImageBitmap is NOT one of those: bloxd's whole texture-pack setup\n"
"  // funnels through it (decodeImage -> 2D canvas -> atlas). Returning a 1x1\n"
"  // stub meant every decoded texture had no pixels, so the atlas never formed\n"
"  // and the loading overlay sat at 0% forever. Real sources are resolved below:\n"
"  // live canvas, a bitmap we already decoded, or a URL/data-URI decoded by the\n"
"  // native bridge (__nfuBitmapPixels -> 'W,H,<base64 RGBA>').\n"
"  var _cvBitmap = function(cv){\n"
"    var w = cv.width | 0, h = cv.height | 0, px = _cvPix(cv);\n"
"    return { width: w, height: h, data: px, _canvas: cv, close: function(){} };\n"
"  };\n"
"  var _bmFromPixels = function(w, h, bytes){\n"
"    var data = new Uint8ClampedArray(w * h * 4), i, n = Math.min(bytes.length, data.length);\n"
"    for (i = 0; i < n; i++) data[i] = bytes[i];\n"
"    return { width: w, height: h, data: data, close: function(){} };\n"
"  };\n"
"  var _bmFromUrl = function(url){\n"
"    if (typeof g.__nfuBitmapPixels !== 'function') return null;\n"
"    var s = '';\n"
"    try { s = String(g.__nfuBitmapPixels(url)); } catch(e){ s = ''; }\n"
"    if (!s) return null;\n"
"    var i1 = s.indexOf(','), i2 = s.indexOf(',', i1 + 1);\n"
"    if (i1 < 0 || i2 < 0) return null;\n"
"    var w = parseInt(s.slice(0, i1), 10), h = parseInt(s.slice(i1 + 1, i2), 10);\n"
"    if (!w || !h || w * h * 4 > 64000000) return null;\n"
"    var bytes = _b64dec(s.slice(i2 + 1));\n"
"    if (!bytes || bytes.length < w * h * 4) return null;\n"
"    return _bmFromPixels(w, h, bytes);\n"
"  };\n"
"  // ''NFUCV:<id>'' is the canvas handle emitted by toDataURL(); resolve it back to\n"
"  // the canvas that produced the pixels, no image decoding involved.\n"
"  var _bmFromTag = function(txt){\n"
"    if (!txt || txt.length < 6 || txt.slice(0, 6) !== 'NFUCV:') return null;\n"
"    var cv = (g.__cvStore || [])[parseInt(txt.slice(6), 10)];\n"
"    return cv ? _cvBitmap(cv) : null;\n"
"  };\n"
"  g.createImageBitmap = function(src, opts){\n"
"    return new Promise(function(res, rej){\n"
"      try {\n"
"        if (src && src._tag === 'canvas'){ res(_cvBitmap(src)); return; }\n"
"        if (src && src.width && src.data){ res(src); return; }\n"
"        if (src && typeof src._bytes === 'function'){\n"
"          var by = src._bytes();\n"
"          if (by && by.length){\n"
"            var tag2 = '', i2;\n"
"            for (i2 = 0; i2 < by.length && i2 < 12; i2++) tag2 += String.fromCharCode(by[i2]);\n"
"            var bm0 = _bmFromTag(tag2);\n"
"            if (bm0){ res(bm0); return; }\n"
"            var du = 'data:' + (src.type || 'image/png') + ';base64,' + _b64enc(by);\n"
"            var bm1 = _bmFromUrl(du);\n"
"            if (bm1){ res(bm1); return; }\n"
"          }\n"
"          rej(new Error('createImageBitmap: blob decode failed')); return;\n"
"        }\n"
"        var u = '';\n"
"        if (typeof src === 'string') u = src;\n"
"        else if (src && (src._srcUrl || src._url)) u = String(src._srcUrl || src._url);\n"
"        if (!u){ rej(new Error('createImageBitmap: unsupported source')); return; }\n"
"        var mk = /^data:image\\/x-nfucv(;base64)?,(.*)$/.exec(u);\n"
"        if (mk){\n"
"          var payload = mk[2], tag3 = mk[1] ? '' : payload;\n"
"          if (mk[1]){ var pb = _b64dec(payload), i3; for (i3 = 0; i3 < pb.length && i3 < 12; i3++) tag3 += String.fromCharCode(pb[i3]); }\n"
"          var bm4 = _bmFromTag(tag3);\n"
"          if (bm4){ res(bm4); return; }\n"
"          rej(new Error('createImageBitmap: stale canvas handle')); return;\n"
"        }\n"
"        var bm = _bmFromUrl(/^data:|^https?:|^\\//.test(u) ? (u.charAt(0) === '/' ? nfuResolveUrl(u) : u) : nfuResolveUrl(u));\n"
"        if (bm){ res(bm); return; }\n"
"        try { print('[BITMAP-MISS] ' + String(u).slice(0, 120)); } catch(pe){}\n"
"        rej(new Error('createImageBitmap: decode failed'));\n"
"      } catch(e){ rej(e); }\n"
"    });\n"
"  };\n"
"  if (typeof g.Audio === 'undefined')\n"
"    g.Audio = function(){ this.play = function(){ return Promise.resolve(); }; this.pause = function(){}; this.canPlayType = function(){ return ''; }; };\n"
"  // ---- audio graph / perf observer / visualViewport stubs --------------\n"
"  // The Cloudflare challenge frame and bloxd's music path both reach for\n"
"  // APIs a desktop browser always has. A missing one shows up as a bare\n"
"  // \"not a function (got undefined)\" from a minified caller, which is exactly\n"
"  // the error that ended the Turnstile run and (with it) the greenlight token.\n"
"  if (typeof g.PerformanceObserver === 'undefined'){\n"
"    g.PerformanceObserver = function(cb){ this._cb = cb; };\n"
"    g.PerformanceObserver.prototype.observe = function(){};\n"
"    g.PerformanceObserver.prototype.disconnect = function(){};\n"
"    g.PerformanceObserver.prototype.takeRecords = function(){ return []; };\n"
"    g.PerformanceObserver.supportedEntryTypes = ['navigation','resource','longtask','mark','measure'];\n"
"  }\n"
"  function _nfuParam(v){ return { value: v, defaultValue: v, minValue: -1e9, maxValue: 1e9,\n"
"    setValueAtTime: function(){ return this; }, linearRampToValueAtTime: function(){ return this; },\n"
"    exponentialRampToValueAtTime: function(){ return this; }, setTargetAtTime: function(){ return this; },\n"
"    cancelScheduledValues: function(){ return this; }, setValueCurveAtTime: function(){ return this; } }; }\n"
"  // Spatial audio: the engine drives PannerNode AudioParams (positionX/Y/Z, the\n"
"  // orientation triple and the cone fields) directly. Any of them missing made\n"
"  // the tick loop throw 'cannot read property positionX of undefined' every\n"
"  // frame, so nothing was ever submitted for drawing.\n"
"  function _nfuAudioNode(o){ o = o || {}; var n = { connect: function(){ return this; },\n"
"    disconnect: function(){}, start: function(){}, stop: function(){},\n"
"    gain: _nfuParam(1), frequency: _nfuParam(440), detune: _nfuParam(0), Q: _nfuParam(1),\n"
"    playbackRate: _nfuParam(1), pan: _nfuParam(0),\n"
"    positionX: _nfuParam(0), positionY: _nfuParam(0), positionZ: _nfuParam(0),\n"
"    orientationX: _nfuParam(0), orientationY: _nfuParam(0), orientationZ: _nfuParam(0),\n"
"    forwardX: _nfuParam(0), forwardY: _nfuParam(0), forwardZ: _nfuParam(-1),\n"
"    upX: _nfuParam(0), upY: _nfuParam(1), upZ: _nfuParam(0),\n"
"    type: 'sine', buffer: null, loop: false, loopStart: 0, loopEnd: 0,\n"
"    distanceModel: 'inverse', panningModel: 'equalpower', refDistance: 1,\n"
"    maxDistance: 10000, rolloffFactor: 1, coneInnerAngle: 360, coneOuterAngle: 360,\n"
"    coneOuterGain: 0, channelCount: 2, fftSize: 2048, frequencyBinCount: 1024 };\n"
"    for (var k in o) n[k] = o[k]; return n; }\n"
"  if (typeof g.AudioContext === 'undefined'){\n"
"    g.AudioContext = function(){ this.state = 'running'; this.sampleRate = 48000; this.currentTime = 0;\n"
"      // EventTarget surface: bloxd's audio engine installs a statechange listener\n"
"      // on the context; without addEventListener the tick loop threw\n"
"      // 'not a function (got undefined)' every frame and never drew.\n"
"      var _ls = {}; this._ls = _ls;\n"
"      this.addEventListener = function(t, f){ (_ls[t] || (_ls[t] = [])).push(f); };\n"
"      this.removeEventListener = function(t, f){ var a = _ls[t]; if (!a) return; for (var i = 0; i < a.length; i++) if (a[i] === f) a.splice(i, 1); };\n"
"      this.dispatchEvent = function(e){ var a = _ls[e && e.type]; if (a) for (var i = 0; i < a.length; i++) try { a[i](e); } catch(x){} return true; };\n"
"      this.destination = { connect: function(){ return this; }, disconnect: function(){}, channelCount: 2 };\n"
"      this.createOscillator = _nfuAudioNode; this.createGain = _nfuAudioNode;\n"
"      this.createAnalyser = _nfuAudioNode; this.createBufferSource = _nfuAudioNode;\n"
"      this.createDelay = _nfuAudioNode; this.createBiquadFilter = _nfuAudioNode;\n"
"      this.createPanner = _nfuAudioNode; this.createStereoPanner = _nfuAudioNode;\n"
"      this.createConvolver = _nfuAudioNode; this.createDynamicsCompressor = _nfuAudioNode;\n"
"      this.createWaveShaper = _nfuAudioNode; this.createChannelMerger = _nfuAudioNode;\n"
"      this.createChannelSplitter = _nfuAudioNode;\n"
"      this.listener = _nfuAudioNode(); this.listener.setPosition = function(){};\n"
"      this.listener.setOrientation = function(){};\n"
"      this.createBuffer = function(ch, len){ len = len || 1024; var bufs = [];\n"
"        for (var bi = 0; bi < ch; bi++) bufs.push(new Float32Array(len));\n"
"        return { numberOfChannels: ch, length: len, sampleRate: 48000, duration: len / 48000,\n"
"                 getChannelData: function(i){ return bufs[i] || bufs[0]; } }; };\n"
"      this.startRendering = function(){ return Promise.resolve(this.createBuffer(1, 1024)); };\n"
"      this.resume = function(){ return Promise.resolve(); }; this.suspend = function(){ return Promise.resolve(); };\n"
"      this.close = function(){ return Promise.resolve(); }; };\n"
"    g.webkitAudioContext = g.AudioContext;\n"
"  }\n"
"  if (typeof g.visualViewport === 'undefined')\n"
"    g.visualViewport = { width: (g.innerWidth || 0), height: (g.innerHeight || 0), scale: 1,\n"
"                         offsetLeft: 0, offsetTop: 0, pageLeft: 0, pageTop: 0,\n"
"                         onresize: null, onscroll: null,\n"
"                         addEventListener: function(){}, removeEventListener: function(){} };\n"
"  // ---- microtask / structured clone / MessageChannel --------------------\n"
"  if (typeof g.queueMicrotask === 'undefined')\n"
"    g.queueMicrotask = function(f){ Promise.resolve().then(function(){ try { f(); } catch(e){} }); };\n"
"  // reportError: used by React's error paths and fetch wrappers; a missing\n"
"  // global throws synchronously inside a promise chain, which app code often\n"
"  // \"handles\" by retrying — an unbounded retry loop.\n"
"  if (typeof g.reportError === 'undefined')\n"
"    g.reportError = function(e){ try { print('[REPORT-ERROR] ' + (e && e.message ? e.message : e)); } catch(pe){} };\n"
"  if (typeof g.__REACT_DEVTOOLS_GLOBAL_HOOK__ === 'undefined')\n"
"    g.__REACT_DEVTOOLS_GLOBAL_HOOK__ = { inject: function(){}, onCommitFiberRoot: function(){}, onCommitFiberMount: function(){}, supportsFiber: true, renderers: {} };\n"
"  if (typeof g.MSApp === 'undefined') g.MSApp = { execUnsafeLocalFunction: function(f){ return f && f(); } };\n"
"  if (typeof g.structuredClone === 'undefined')\n"
"    g.structuredClone = function(v){ try { return JSON.parse(JSON.stringify(v)); } catch(e){ return v; } };\n"
"  // AMD loader placeholder: pages like baidu.com run inline scripts that call\n"
"  // require([...]) before their esl/requirejs loader script finishes loading.\n"
"  // A missing global aborts those scripts; a no-op stub lets the page boot,\n"
"  // and the real loader overwrites these when it arrives.\n"
"  // NOTE: do NOT define a global `define` (even with amd=true): webpack UMD\n"
"  // bundles detect it and take the AMD branch, so the module never registers\n"
"  // on window and the page breaks (verified: bloxd.io went blank).\n"
"  if (typeof g.require === 'undefined') {\n"
"    g.require = function(){ var cb = arguments[arguments.length-1]; if (typeof cb === 'function') try { cb(); } catch(e){} };\n"
"    g.require.config = function(){}; g.require.toUrl = function(s){ return String(s); };\n"
"  }\n"
"  if (typeof g.MessageChannel === 'undefined'){\n"
"    g.MessagePort = function(p){ this._peer = p; this._h = []; this.onmessage = null; this._isNfuPort = true; };\n"
"    g.MessagePort.prototype.postMessage = function(d){ try { g.__nfuPortN = (g.__nfuPortN || 0) + 1; if (g.__nfuPortN <= 12) print('[PORTMSG] ' + String(d && d.type ? d.type : typeof d)); } catch(pe){} var peer = this._peer, self = this; (g.__nfuTimers = g.__nfuTimers || []).push(function(){ var ev = { data: d, type: 'message', target: peer }; if (peer.onmessage) try { peer.onmessage(ev); } catch(e){} for (var i = 0; i < peer._h.length; i++) try { peer._h[i](ev); } catch(e){} }); };\n"
"    g.MessagePort.prototype.addEventListener = function(t, f){ if (t === 'message' && f) this._h.push(f); };\n"
"    g.MessagePort.prototype.removeEventListener = function(t, f){ var i = this._h.indexOf(f); if (i >= 0) this._h.splice(i, 1); };\n"
"    g.MessagePort.prototype.start = function(){}; g.MessagePort.prototype.close = function(){};\n"
"    g.MessageChannel = function(){ var a = new g.MessagePort(null), b = new g.MessagePort(null); a._peer = b; b._peer = a; this.port1 = a; this.port2 = b; try { g.__nfuMcN = (g.__nfuMcN || 0) + 1; if (g.__nfuMcN <= 6) { var st = ''; try { st = String(new Error().stack || '').replace(/\\n/g, ' | ').slice(0, 260); } catch(se){} print('[MCHAN] #' + g.__nfuMcN + ' STACK=' + st); } } catch(pe){} };\n"
"  }\n"
"  // BroadcastChannel: minijs installs a bare `function(){}` stub for any\n"
"  // missing window constructor, so `typeof BroadcastChannel` was 'function'\n"
"  // and bloxd's page-kill marker successfully created one - then threw on\n"
"  // teardown at `channel.close()` (undefined) inside the play() finally block.\n"
"  // That TypeError is what escaped to chunk89's .catch and produced the\n"
"  // generic 'Unexpected Error' popup + bounce back to the home page, and it\n"
"  // also hid the real game error. Single-tab semantics are enough here: named\n"
"  // channels deliver to their siblings, close() unregisters.\n"
"  try {\n"
"    if (!g.BroadcastChannel || !g.BroadcastChannel.prototype || !g.BroadcastChannel.prototype.postMessage) {\n"
"      var _bcReg = {};\n"
"      var _BC = function(name){\n"
"        this.name = String(name === undefined ? '' : name);\n"
"        this.onmessage = null;\n"
"        this._lis = [];\n"
"        this._closed = false;\n"
"        (_bcReg[this.name] = _bcReg[this.name] || []).push(this);\n"
"      };\n"
"      _BC.prototype.postMessage = function(data){\n"
"        if (this._closed) return;\n"
"        var peers = _bcReg[this.name] || [], i, p, ev;\n"
"        for (i = 0; i < peers.length; i++){\n"
"          p = peers[i];\n"
"          if (p === this || p._closed) continue;\n"
"          ev = { type: 'message', data: data, target: p, currentTarget: p, origin: '' };\n"
"          try { if (typeof p.onmessage === 'function') p.onmessage(ev); } catch(e){}\n"
"          for (var j = 0; j < p._lis.length; j++){ try { p._lis[j](ev); } catch(e){} }\n"
"        }\n"
"      };\n"
"      _BC.prototype.addEventListener = function(t, f){ if (t === 'message' && typeof f === 'function') this._lis.push(f); };\n"
"      _BC.prototype.removeEventListener = function(t, f){ var i = this._lis.indexOf(f); if (i >= 0) this._lis.splice(i, 1); };\n"
"      _BC.prototype.dispatchEvent = function(ev){ if (typeof this.onmessage === 'function') this.onmessage(ev); return true; };\n"
"      _BC.prototype.close = function(){\n"
"        this._closed = true;\n"
"        var a = _bcReg[this.name] || [], i = a.indexOf(this);\n"
"        if (i >= 0) a.splice(i, 1);\n"
"      };\n"
"      g.BroadcastChannel = _BC;\n"
"    }\n"
"  } catch(_bce){}\n"
"  // ---- Image ------------------------------------------------------------\n"
"  // Preloaders do `new Image(); img.src = ...`; returning a real <img> node\n"
"  // keeps the request in the image cache and the element usable as a DOM kid.\n"
"  if (typeof g.Image === 'undefined'){\n"
"    g.Image = function(w, h){ var el = g.document.createElement('img'); if (w) el.width = w; if (h) el.height = h; return el; };\n"
"  }\n"
"  // ---- DOM constructor identity -----------------------------------------\n"
"  // Real apps patch these (Element.prototype.matches = ...) or subclass\n"
"  // them (class Bus extends EventTarget). With them undefined the first\n"
"  // such line threw \"cannot read property 'prototype' of undefined\" during\n"
"  // module evaluation and the whole bundle died (chat.deepseek.com).\n"
"  g.Node = Node;\n"
"  g.Element = Node; g.HTMLElement = Node;\n"
"  g.HTMLDivElement = Node; g.HTMLSpanElement = Node; g.HTMLInputElement = Node;\n"
"  g.HTMLButtonElement = Node; g.HTMLAnchorElement = Node; g.HTMLImageElement = Node;\n"
"  g.HTMLCanvasElement = Node; g.HTMLFormElement = Node; g.HTMLSelectElement = Node;\n"
"  g.HTMLTextAreaElement = Node; g.HTMLIFrameElement = Node; g.HTMLScriptElement = Node;\n"
"  g.HTMLStyleElement = Node; g.HTMLTemplateElement = Node;\n"
"  g.CharacterData = Node; g.Text = Node; g.Comment = Node;\n"
"  g.HTMLCollection = function(){}; g.NodeList = function(){};\n"
"  g.Document = NfuDoc; g.HTMLDocument = NfuDoc; g.DocumentFragment = NfuDoc;\n"
"  g.Event = function(type, init){ init = init || {}; this.type = String(type);\n"
"    this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable;\n"
"    this.target = init.target || null; this.currentTarget = null;\n"
"    this.defaultPrevented = false; this.timeStamp = Date.now();\n"
"    this.detail = init.detail; this.key = init.key; this.code = init.code;\n"
"    this.clientX = init.clientX || 0; this.clientY = init.clientY || 0;\n"
"    this.button = init.button || 0; this.buttons = init.buttons || 0;\n"
"    this.isTrusted = false; this.eventPhase = 2; };\n"
"  g.Event.prototype.preventDefault = function(){ this.defaultPrevented = true; };\n"
"  g.Event.prototype.stopPropagation = function(){ this._stop = true; };\n"
"  g.Event.prototype.stopImmediatePropagation = function(){ this._stop = true; };\n"
"  g.Event.prototype.composedPath = function(){ return this.target ? [this.target] : []; };\n"
"  g.Event.prototype.initEvent = function(t, b, c){ this.type = String(t); this.bubbles = !!b; this.cancelable = !!c; };\n"
"  g.CustomEvent = g.Event; g.MouseEvent = g.Event; g.PointerEvent = g.Event;\n"
"  g.KeyboardEvent = g.Event; g.FocusEvent = g.Event; g.InputEvent = g.Event;\n"
"  g.WheelEvent = g.Event; g.TouchEvent = g.Event; g.DragEvent = g.Event;\n"
"  g.AnimationEvent = g.Event; g.TransitionEvent = g.Event; g.ErrorEvent = g.Event;\n"
"  g.DOMRect = function(x, y, w, h){ this.x = x || 0; this.y = y || 0;\n"
"    this.width = w || 0; this.height = h || 0; this.top = this.y; this.left = this.x;\n"
"    this.right = this.x + this.width; this.bottom = this.y + this.height; };\n"
"  g.DOMRectReadOnly = g.DOMRect;\n"
"  // DOMException: core-js's polyfill module reads\n"
"  //   var C = globalThis.DOMException; var P = C.prototype;\n"
"  // unguarded — with it missing, every bundle that imports the polyfill\n"
"  // throws \"cannot read property 'prototype' of undefined\" at eval time\n"
"  // and never mounts (chat.deepseek.com).\n"
"  if (typeof g.DOMException === 'undefined'){\n"
"    g.DOMException = function(msg, name){ this.message = String(msg == null ? '' : msg); this.name = String(name || 'Error'); this.stack = this.name + ': ' + this.message; };\n"
"    g.DOMException.prototype = Object.create((typeof Error !== 'undefined') ? Error.prototype : Object.prototype);\n"
"    g.DOMException.prototype.constructor = g.DOMException;\n"
"  }\n"
"  if (typeof g.Request === 'undefined')\n"
"    g.Request = function(url, o){ o = o || {}; this.url = String(url);\n"
"      this.method = String(o.method || 'GET').toUpperCase(); this.headers = o.headers || {};\n"
"      this.body = o.body; this._t = (typeof o.body === 'string') ? o.body : '';\n"
"      this.credentials = o.credentials || 'same-origin'; this.mode = o.mode || 'cors'; };\n"
"  if (typeof g.Request !== 'undefined' && g.Request.prototype)\n"
"    g.Request.prototype.clone = function(){ return new g.Request(this.url, this); };\n"
"  if (typeof g.FileReader === 'undefined')\n"
"    g.FileReader = function(){ var self = this; this.readyState = 0; this.result = null;\n"
"      this.onload = null; this.onerror = null; this.onloadend = null; this.onprogress = null;\n"
"      function fin(r){ self.readyState = 2; self.result = r;\n"
"        if (self.onprogress) try { self.onprogress({ loaded: 1, total: 1 }); } catch(e){}\n"
"        if (self.onload) try { self.onload({ target: self }); } catch(e){}\n"
"        if (self.onloadend) try { self.onloadend({ target: self }); } catch(e){} }\n"
"      this.readAsText = function(b){ var t = ''; try { t = b && b.text ? '' : String(b); } catch(e){}\n"
"        if (b && b._bytes) { try { t = _utf8dec(b._bytes()); } catch(e){} }\n"
"        else if (b && b.text) b.text().then(function(v){ fin(String(v)); });\n"
"        else fin(t);\n"
"        if (!(b && b.text)) (g.__nfuTimers = g.__nfuTimers || []).push(function(){ if (self.result === null) fin(t); }); };\n"
"      this.readAsArrayBuffer = function(b){ var ab = null;\n"
"        try { if (b && b._bytes) ab = b._bytes().buffer; } catch(e){}\n"
"        (g.__nfuTimers = g.__nfuTimers || []).push(function(){ fin(ab); }); };\n"
"      this.readAsDataURL = function(b){ var s = '';\n"
"        try { if (b && b._bytes) s = 'data:' + ((b._m && b._m.type) || 'application/octet-stream') + ';base64,' + _b64enc(b._bytes()); } catch(e){}\n"
"        (g.__nfuTimers = g.__nfuTimers || []).push(function(){ fin(s); }); };\n"
"      this.abort = function(){}; this.addEventListener = function(t, f){ self['on' + t] = f; }; };\n"
"  g.IntersectionObserver = _observer(function(o, t){ if (o._cb) o._cb([{ target: t, isIntersecting: true, intersectionRatio: 1, boundingClientRect: t.getBoundingClientRect ? t.getBoundingClientRect() : {}, time: Date.now() }], o); });\n"
"  g.ResizeObserver = _observer(function(o, t){ if (o._cb) { var w = nfuBoxW(t, 0), h = nfuBoxH(t, 0); if (!h) h = (g.innerHeight || 600); o._cb([{ target: t, contentRect: { width: w, height: h, top: 0, left: 0, right: w, bottom: h, x: 0, y: 0 }, borderBoxSize: [{ inlineSize: w, blockSize: h }], contentBoxSize: [{ inlineSize: w, blockSize: h }] }], o); } });\n"
"  g.MutationObserver = function(cb){ this._cb = cb; this.observe = function(){}; this.disconnect = function(){}; this.takeRecords = function(){ return []; }; };\n"
"  g.Headers = function(init){ this._m = {}; if (init){ if (init.forEach) init.forEach(function(v, k){ this._m[String(k).toLowerCase()] = String(v); }, this); else for (var k in init) this._m[String(k).toLowerCase()] = String(init[k]); } };\n"
"  g.Headers.prototype.get = function(k){ var v = this._m[String(k).toLowerCase()]; return v === undefined ? null : v; };\n"
"  g.Headers.prototype.set = function(k, v){ this._m[String(k).toLowerCase()] = String(v); };\n"
"  g.Headers.prototype.has = function(k){ return this._m[String(k).toLowerCase()] !== undefined; };\n"
"  g.Headers.prototype.append = function(k, v){ this.set(k, v); };\n"
"  g.Headers.prototype['delete'] = function(k){ delete this._m[String(k).toLowerCase()]; };\n"
"  g.Headers.prototype.forEach = function(fn){ for (var k in this._m) fn(this._m[k], k, this); };\n"
"  g.Headers.prototype.entries = function(){ var o = this._m, ks = Object.keys(o), i = 0; return { next: function(){ if (i < ks.length) { var k2 = ks[i++]; return { value: [k2, o[k2]], done: false }; } return { done: true }; } }; };\n"
"  g.Headers.prototype[Symbol.iterator] = function(){ return this.entries(); };\n"
"  g.URL = function(u, base){ u = String(u || ''); var m = u.match(/^([a-zA-Z]+:)\\/\\/([^\\/?#]*)([^?#]*)(\\?[^#]*)?(#.*)?$/); if (!m && base){ var b = String(base).replace(/[^\\/]*$/, ''); u = b + u; m = u.match(/^([a-zA-Z]+:)\\/\\/([^\\/?#]*)([^?#]*)(\\?[^#]*)?(#.*)?$/); } this.href = u; this.protocol = m ? m[1] : 'https:'; this.host = m ? m[2] : ''; var hp = this.host.split(':'); this.hostname = hp[0]; this.port = hp[1] || ''; this.pathname = m ? m[3] : '/'; this.search = m && m[4] ? m[4] : ''; this.hash = m && m[5] ? m[5] : ''; this.origin = this.protocol + '//' + this.host; this.searchParams = g.URLSearchParams ? new g.URLSearchParams(this.search) : { get: function(){ return null; }, set: function(){}, has: function(){ return false; }, forEach: function(){} }; this.toString = function(){ return this.href; }; };\n"
"  // ---- base64 (btoa/atob + object URLs) --------------------------------\n"
"  var _b64T = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';\n"
"  function _b64enc(u){\n"
"    var s = [], i, n = u.length;\n"
"    for (i = 0; i + 2 < n; i += 3){\n"
"      var a = u[i], b = u[i+1], c = u[i+2];\n"
"      s.push(_b64T.charAt(a >> 2), _b64T.charAt(((a & 3) << 4) | (b >> 4)),\n"
"             _b64T.charAt(((b & 15) << 2) | (c >> 6)), _b64T.charAt(c & 63));\n"
"    }\n"
"    var r = n - i;\n"
"    if (r === 1){ var x = u[i]; s.push(_b64T.charAt(x >> 2), _b64T.charAt((x & 3) << 4), '=='); }\n"
"    else if (r === 2){ var y = u[i], z = u[i+1]; s.push(_b64T.charAt(y >> 2), _b64T.charAt(((y & 3) << 4) | (z >> 4)), _b64T.charAt((z & 15) << 2), '='); }\n"
"    return s.join('');\n"
"  }\n"
"  function _b64dec(str){\n"
  "    if (String(str).length > 3000 && (g.__nfuB64N = (g.__nfuB64N || 0) + 1) <= 60) try { print(\'[B64] n=\' + g.__nfuB64N + \' in=\' + String(str).length + \' head=\' + String(str).slice(0, 28)); } catch(pb){}\n"
"    str = String(str).replace(/[^A-Za-z0-9+\\/]/g, '');\n"
"    var out = [], i, v = 0, bits = 0;\n"
"    for (i = 0; i < str.length; i++){\n"
"      var d = _b64T.indexOf(str.charAt(i));\n"
"      if (d < 0) continue;\n"
"      v = (v << 6) | d; bits += 6;\n"
"      if (bits >= 8){ bits -= 8; out.push((v >> bits) & 0xFF); }\n"
"    }\n"
"    return new Uint8Array(out);\n"
"  }\n"
"  g.btoa = function(s){ var str = String(s), u = new Uint8Array(str.length), i; for (i = 0; i < str.length; i++) u[i] = str.charCodeAt(i) & 0xFF; return _b64enc(u); };\n"
"  g.atob = function(b){ var u = _b64dec(b), s = '', i; if (String(b).length > 20000 && (g.__nfuAtoN = (g.__nfuAtoN||0) + 1) <= 60) try { print('[ATOB] n=' + g.__nfuAtoN + ' b64len=' + String(b).length + ' bytes=' + u.length + ' head=' + String.fromCharCode(u[0]||0, u[1]||0, u[2]||0, u[3]||0)); } catch(pa){} for (i = 0; i < u.length; i++) s += String.fromCharCode(u[i]); return s; };\n"
"  // A failing JSON.parse is a common silent boot blocker (a response body the\n"
"  // app expected to be JSON arrived as HTML/empty). Surface the payload head so\n"
"  // the missing endpoint is identifiable from the log instead of a bare\n"
"  // SyntaxError with no URL.\n"
"  try {\n"
"    if (typeof JSON !== 'undefined' && !JSON.__nfuWrapped){\n"
"      var _jsonParse = JSON.parse;\n"
"      JSON.parse = function(txt, rev){\n"
"        try { return _jsonParse(txt, rev); }\n"
"        catch(e){ try { g.__nfuJfN = (g.__nfuJfN || 0) + 1; if (g.__nfuJfN <= 3) { var st = ''; try { st = String(new Error().stack || '').replace(/\\n/g, ' | ').slice(0, 400); } catch(se){} print('[JSON-FAIL] ' + String(txt).slice(0, 60) + ' STACK=' + st); } } catch(pe){} throw e; }\n"
"      };\n"
"      JSON.__nfuWrapped = true;\n"
"    }\n"
"  } catch(e){}\n"
"  var _blobN = 0;\n"
"  // Object URLs are data: URLs. The renderer resolves images and downloads\n"
"  // itself, so a blob: handle it cannot open would leave every canvas export /\n"
"  // file preview blank; base64 of the same bytes just works.\n"
"  g.URL.createObjectURL = function(b){\n"
"    try {\n"
"      if (b && b._bytes){\n"
"        var u = b._bytes();\n"
"        return 'data:' + (b.type || 'application/octet-stream') + ';base64,' + _b64enc(u);\n"
"      }\n"
"    } catch(e){}\n"
"    return 'blob:nefu/' + (++_blobN);\n"
"  };\n"
"  g.URL.revokeObjectURL = function(){};\n"
"  g.URL.canParse = function(u){ return !!String(u).match(/^[a-zA-Z]+:\\/\\//); };\n"
"  g.URLSearchParams = function(init){ var m = {}; var add = function(k, v){ (m[k] = m[k] || []).push(String(v)); }; if (typeof init === 'string'){ var q = init.replace(/^\\?/, ''), ps = q ? q.split('&') : [], i; for (i = 0; i < ps.length; i++){ var kv = ps[i].split('='); if (kv[0]) add(decodeURIComponent(kv[0]), decodeURIComponent(kv[1] || '')); } } else if (init && init.forEach) init.forEach(function(v, k){ add(k, v); }); this.get = function(k){ return m[k] ? m[k][0] : null; }; this.getAll = function(k){ return m[k] || []; }; this.has = function(k){ return !!m[k]; }; this.set = function(k, v){ m[k] = [String(v)]; }; this.append = function(k, v){ add(k, v); }; this['delete'] = function(k){ delete m[k]; }; this.forEach = function(fn){ for (var k in m) for (var i = 0; i < m[k].length; i++) fn(m[k][i], k, this); }; this.toString = function(){ var o = []; for (var k in m) for (var i = 0; i < m[k].length; i++) o.push(encodeURIComponent(k) + '=' + encodeURIComponent(m[k][i])); return o.join('&'); }; };\n"
"  g.getComputedStyle = function(el){ var st = styleApi((el && el._style) || {}); return { getPropertyValue: function(k){ return st.getPropertyValue(k); }, color: st.color || 'rgb(0, 0, 0)', backgroundColor: st.backgroundColor || 'rgba(0, 0, 0, 0)', display: st.display || 'block', position: st.position || 'static', width: '300px', height: '24px', opacity: st.opacity || '1', visibility: st.visibility || 'visible', length: 0, item: function(){ return ''; } }; };\n"
"  g.CSS = g.CSS || {};\n"
"  g.CSS.escape = g.CSS.escape || function(s){ return String(s).replace(/([^a-zA-Z0-9_-])/g, '\\\\$1'); };\n"
"  g.CSS.supports = g.CSS.supports || function(){ return true; };\n"
"  g.scrollTo = function(){};\n"
"  g.scrollBy = function(){};\n"
"  g.focus = function(){};\n"
"  g.blur = function(){};\n"
"  g.open = function(u, n, f){\n"
"    // A real browser opens a second window; this one has a single view, so only\n"
"    // an explicit _self/_top target navigates. Everything else is logged so a\n"
"    // silently swallowed popup is at least visible in the console.\n"
"    if (u) { try { print('[OPEN] ' + u + ' target=' + (n || '')); } catch(e){}\n"
"             if (n === '_self' || n === '_top' || n === '_parent') g.__nfuNav(String(u), 0); }\n"
"    return null;\n"
"  };\n"
"  g.close = function(){};\n"
"  g.postMessage = function(d, org){ gWinDispatch(d, org, null); };\n"
"  g.history = { length: 1, state: null, scrollRestoration: 'auto',\n"
"    pushState: function(s, t, u){ this.state = s; try { print('[NAV] push ' + String(u)); } catch(e){} try { var rg = g.__nfuTagRing || []; print('[TAG-RING] ' + rg.slice(-45).join(',')); } catch(_tre){} try { var _bt = ''; try { _bt = String(g.document.body.innerText || '').replace(/\\s+/g, ' ').slice(0, 260); } catch(_be){ _bt = '?'; } print('[NAV-BODY] ' + _bt); } catch(e2){} try { if (g.setTimeout) g.setTimeout(function(){ try { var _t2 = String((g.document.body.textContent || g.document.body.innerText) || '').replace(/\\s+/g, ' ').slice(0, 600); print('[NAV-BODY-LATE] ' + _t2); var _h = ''; try { _h = String(g.document.body.innerHTML || '').replace(/\\s+/g, ' '); } catch(_he){} var _hl = _h.toLowerCase(); var _mk = ''; if (_hl.indexOf('face-dizzy') >= 0) _mk += ' dizzy'; if (_hl.indexOf('tryagain') >= 0) _mk += ' tryAgain'; if (_hl.indexOf('promptpopup') >= 0) _mk += ' promptPopup'; if (_hl.indexOf('error') >= 0) _mk += ' error'; print('[NAV-MARK]' + (_mk || ' none') + ' len=' + _h.length); print('[NAV-HTML-LATE] ' + _h.slice(0, 3800)); } catch(_e5){} }, 1200); } catch(_e6){} if (u) g.__nfuSetLoc(String(u)); },\n"
"    replaceState: function(s, t, u){ this.state = s; try { print('[NAV] replace ' + String(u)); } catch(e){} if (u) g.__nfuSetLoc(String(u)); },\n"
"    back: function(){}, forward: function(){}, go: function(){} };\n"
"  // client-side routing: SPAs change the address bar without a page load, so\n"
"  // only the location fields move (the app re-renders itself)\n"
"  g.__nfuSetLoc = function(u){\n"
"    var loc = g.location || (g.location = {});\n"
"    var m = String(u).match(/^([a-zA-Z]+:)?\\/\\/([^\\/?#]*)([^?#]*)(\\?[^#]*)?(#.*)?$/);\n"
"    if (m){ loc.__href = u; if (m[1]) loc.protocol = m[1]; loc.host = m[2]; loc.hostname = String(m[2]).split(':')[0]; loc.pathname = m[3] || '/'; loc.search = m[4] || ''; loc.hash = m[5] || ''; }\n"
"    else { var q = String(u).indexOf('?'), h = String(u).indexOf('#'); loc.pathname = (q >= 0 ? String(u).slice(0, q) : String(u)).split('#')[0]; if (q >= 0) loc.search = '?' + String(u).slice(q + 1, h >= 0 ? h : undefined); if (h >= 0) loc.hash = String(u).slice(h); }\n"
"    loc.__href = (loc.protocol || 'https:') + '//' + (loc.host || '') + (loc.pathname || '/') + (loc.search || '') + (loc.hash || '');\n"
"    if (g.document) try { g.document.URL = loc.__href; } catch(e){}\n"
"    if (g.dispatchEvent) try { g.dispatchEvent({ type: 'popstate', state: g.history ? g.history.state : null }); } catch(e){}\n"
"  };\n"
"  // A real navigation (a page load, not an SPA pushState): hand the intent to\n"
"  // the host. It cannot be acted on inside this eval, so the host parks it and\n"
"  // starts the load on its next frame. Falls back to a bare URL update when the\n"
"  // native bridge is missing (unit tests, plain `js` command).\n"
"  g.__nfuNav = function(u, mode){\n"
"    u = (u === undefined || u === null) ? '' : String(u);\n"
"    mode = mode | 0;\n"
"    if (typeof g.__nfuGo === 'function') { try { g.__nfuGo(u, mode); return 1; } catch(e){} }\n"
"    if (u) g.__nfuSetLoc(u);\n"
"    return 0;\n"
"  };\n"
"  // location.href is the one property pages assign to navigate: give it a\n"
"  // setter (a plain data property would just be overwritten silently). The\n"
"  // native side pre-creates window.location with a data property, so this has\n"
"  // to REPLACE it rather than only fill a gap.\n"
"  (function(){\n"
"    var loc = g.location;\n"
"    try {\n"
"      if (!loc.__nfuHrefPatched) {\n"
"        if (loc.href !== undefined && loc.__href === undefined) loc.__href = loc.href;\n"
"        Object.defineProperty(loc, 'href', {\n"
"          configurable: true, enumerable: true,\n"
"          get: function(){ return this.__href || ''; },\n"
"          set: function(v){ this.__href = String(v); g.__nfuNav(String(v), 0); }\n"
"        });\n"
"        loc.__nfuHrefPatched = 1;\n"
"      }\n"
"    } catch(e){}\n"
"    loc.assign = function(u){ g.__nfuNav(String(u), 0); };\n"
"    loc.replace = function(u){ g.__nfuNav(String(u), 0); };\n"
"    loc.reload = function(){ g.__nfuNav('', 2); };\n"
"    loc.toString = function(){ return this.href; };\n"
"    if (g.history) {\n"
"      g.history.back = function(){ g.__nfuNav('', 3); };\n"
"      g.history.forward = function(){ g.__nfuNav('', 4); };\n"
"      g.history.go = function(n){ n = n | 0; if (n < 0) g.__nfuNav('', 3); else if (n > 0) g.__nfuNav('', 4); else g.__nfuNav('', 2); };\n"
"    }\n"
"  })();\n"
" })(this);\n";

// ---------------------------------------------------------------------------
// Transport patch: the base shim's XMLHttpRequest/fetch only ever issued GET
// (via __nfuHttpGet, body + method discarded). bloxd's anonymous auth POST and
// the Colyseus matchmaking POST must carry a body, so they silently degrade to
// GET and the server rejects them -> "Can't connect to Bloxd". This patch
// overrides both with versions that forward method + body + headers through the
// native __nfuHttpRequest binding (platform_http_request). The metrics endpoint
// stays emulated (server only accepts POST; we never transmit telemetry) and the
// anon auth call falls back to a local identity when the network is down, so the
// app still reaches its main menu offline. Evaluated right after the main shim.
static const char* const NFU_TRANSPORT_PATCH = R"nfu_patch(
(function(){
  var g = (typeof globalThis !== 'undefined') ? globalThis : this;
  function _buildHeaders(h){
    var s = '';
    try { var ks = Object.keys(h || {}); for (var i = 0; i < ks.length; i++) s += ks[i] + ': ' + h[ks[i]] + '\r\n'; } catch(e){}
    return s;
  }
  // The emulated anonymous identity is defined once by the main shim
  // (g.__nfuIdentityJson) so both shim layers agree on the field set.
  function _identityJson(){
    try { if (typeof g.__nfuIdentityJson === 'function') return g.__nfuIdentityJson(); } catch(e){}
    return '{"token":"nfu-anon-token","_id":"nfu0001","name":"Player","whamm":"nfu0001",' +
           '"anonymizedAccountInfo":{"id":"nfu0001"},"cosmetics":{},"ranks":[],' +
           '"availableUserContextTypes":["ANONYMOUS"],"error":null}';
  }
  // ---- cookie round-trip -----------------------------------------------
  // The native HTTP client hands back the Set-Cookie lines of every response
  // (newline separated, attributes intact). They are replayed through the
  // document jar so a site's session survives across requests - without this
  // every request looks like a brand-new visitor, which breaks any login or
  // consent flow. Cookies are only adopted for, and only sent to, the
  // document's own origin; a cross-origin request must not see them.
  function _hostOf(u){
    var m = String(u == null ? '' : u).match(/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\/([^\/?#]*)/);
    if (!m) return '';
    return String(m[1]).split('@').pop().split(':')[0].toLowerCase();
  }
  function _docHost(){
    try { if (g.location && g.location.hostname) return String(g.location.hostname).toLowerCase(); } catch(e){}
    return '';
  }
  // Cookies are host scoped, but a real browser also sends a parent-domain
  // cookie (Domain=.bloxd.io) to every subdomain. bloxd sets its session on
  // bloxd.io and calls the API on social2.bloxd.io / survival.bloxd.io, so
  // requiring an exact host match silently dropped every cookie and every
  // social call came back 401.
  function _sameCookieDomain(a, b){
    if (!a || !b) return false;
    if (a === b) return true;
    if (a.length > b.length) return a.slice(-(b.length + 1)) === '.' + b;
    return b.slice(-(a.length + 1)) === '.' + a;
  }
  // bloxd's identity endpoint (bloxd.io/index/metrics/cookies) answers with the
  // cookie key/value pairs inside the JSON *body* instead of a Set-Cookie header.
  // A stock browser only works because it also receives that header; our native
  // HTTP path can read HTTP_QUERY_SET_COOKIE but the origin sends none, so the jar
  // stayed empty and every game-server request went out unauthenticated. Adopt the
  // body pairs under the same same-origin rule the header path uses.
  function _jarAdoptBody(url, body){
    if (!body) return;
    var h = _hostOf(url), lh = _docHost();
    if (!lh || !h || !_sameCookieDomain(h, lh)) return;
    var t = String(body).replace(/^\s+/, '');
    if (t.charAt(0) !== '{') return;
    var o = null;
    try { o = JSON.parse(t); } catch(e){ return; }
    if (!o || typeof o !== 'object') return;
    var k, n = 0;
    for (k in o){
      if (!/^[A-Za-z0-9_$%.-]{2,40}$/.test(k)) continue;
      if (typeof o[k] !== 'string' || String(o[k]).length < 8) continue;
      try { g.document.cookie = k + '=' + o[k]; } catch(e){}
      n++;
    }
    if (n) try { print('[CKBODY] ' + h + ' adopted=' + n); } catch(pe){}
  }
  function _jarAdopt(url, sc){
    if (!sc) return;
    var h = _hostOf(url), lh = _docHost();
    if (lh && h && !_sameCookieDomain(h, lh)) return;          // never mix unrelated origins
    var lines = String(sc).split('\n');
    for (var i = 0; i < lines.length; i++){
      var one = lines[i];
      if (!one || one.indexOf('=') <= 0) continue;
      var nm = one.split('=')[0];
      if (nm) try { print('[CK] ' + h + ' ' + nm); } catch(pe){}
      try { g.document.cookie = one; } catch(e){}
    }
  }
  function _jarHeader(url, hstr){
    var h = _hostOf(url), lh = _docHost();
    if (!lh || !h || !_sameCookieDomain(h, lh)) return hstr || '';
    var jar = '';
    try { jar = g.document.cookie || ''; } catch(e){}
    if (!jar) return hstr || '';
    if (hstr && /(^|\r?\n)[ \t]*cookie[ \t]*:/i.test(hstr)) return hstr;   // caller wins
    // Trace what identity actually goes out: the social API answers a bare 401
    // both when the jar is empty and when it carries a rejectable id, so the
    // count and the head of the header are the only things that tell them apart.
    try {
      g.__nfuJarN = (g.__nfuJarN || 0) + 1;
      if (g.__nfuJarN <= 12) print('[JAR] host=' + h + ' n=' + jar.split('; ').length +
                                  ' len=' + jar.length + ' head=' + jar.slice(0, 46));
    } catch(jx){}
    return (hstr || '') + 'Cookie: ' + jar + '\r\n';
  }
  // bloxd hands out a fresh game-server host on every matchmake. Some of those
  // droplets do not answer on 443 at all (the origin still publishes the name, the
  // machine is gone), and the client then retries keep-alive against a dead address
  // forever instead of ever opening the world socket. Worse, it never re-matchmakes
  // on its own, so one unlucky allocation deadlocks the whole session. Probe the
  // host before the client sees the matchmake body: an answered request of ANY
  // status proves the box is up, a timeout means re-matchmake and try another.
  function _preflightMatchmake(r, method, url, body, hstr){
    if (!/\/social\/bloxd-matchmake(\?|$)/.test(String(url))) return r;
    var tries = 0, cur = r;
    while (cur && typeof cur === 'object' && cur.body && tries < 4) {
      var hostm = /"gameServerHost"\s*:\s*"([^"]+)"/.exec(String(cur.body));
      if (!hostm) break;
      var host = String(hostm[1]);
      var probe = null;
      try { probe = g.__nfuHttpRequest('GET', 'https://' + host + '/', '', ''); } catch(pp){ probe = null; }
      if (probe && typeof probe === 'object' && (probe.status | 0) > 0) {
        try { print('[HOST-OK] ' + host + ' st=' + (probe.status|0)); } catch(po){}
        break;
      }
      tries++;
      try { print('[HOST-DEAD] ' + host + ' rematch#' + tries); } catch(pd){}
      var r2 = null;
      try { r2 = g.__nfuHttpRequest(method, url, (body || ''), _jarHeader(url, hstr || '')); } catch(pr){ r2 = null; }
      if (!r2 || typeof r2 !== 'object') break;
      cur = r2;
    }
    return cur || r;
  }
  function _doReq(method, url, body, hstr){
    if (!/\/static\/js\//.test(String(url))) { try { print('[REQ-IN] ' + String(url).slice(0, 120)); } catch(re0){} }
    try {
      // Resolve relative URLs against the document origin before handing the
      // request to native code. Webpack's public path is "/" for bloxd, so the
      // wasm module is fetched as "/static/media/bloxd_wasm_bg.*.wasm" and
      // native winsock/WinINet cannot do origin resolution on its own - the
      // request used to come back as HTTP 0 with an empty body, which bloxd
      // reported as "Bloxd couldn't finish loading. Check your connection".
      url = _absUrl(String(url));
      // One line per dynamic (non-chunk) request: the only way to see which
      // URL a failed API call (401s print no address) actually targeted.
      if (!/\/static\/js\//.test(String(url))) { try { print('[REQ] ' + method + ' ' + String(url).slice(0, 140)); } catch(pe){} }
      if (typeof g.__nfuHttpRequest !== 'function') { try { print('[REQ-NULL] no-native ' + String(url).slice(0,80)); } catch(rn0){} return null; }
      hstr = _jarHeader(url, hstr);
      // The identity POST is the one request whose 420 cannot be explained from
      // the outside: it needs the exact body the page built and the exact header
      // set, so dump both once when the server says no.
      if (/index\/metrics\/cookies/.test(url)) {
        try {
          print('[MCREQ] ' + method + ' blen=' + String(body || '').length +
                ' body=' + String(body || '').slice(0, 180) +
                ' hdr=' + String(hstr || '').replace(/\r?\n/g, '|').slice(0, 340));
        } catch(mc1){}
      }
      // The metric session proof is derived by the page, but the server compares
      // it against its own derivation and answers 420 (client version mismatch)
      // on any difference. Rewrite that one field with the token observed on a
      // real successful request, keeping the rest of the payload intact.
      // Colyseus' join carries the same metric identity as a body field, and the
      // room server authenticates against it: with the page's own placeholder
      // values (3PSIDMC/3PSIDMCPP = "N/A") every join dies on (4215) onAuth
      // failed even though the cookie jar is byte-identical to a real browser's.
      // Swap the whole object for the one assembled from the captured jar,
      // identity blob and session proof.
      if (body && body.indexOf('"metricsCookies":') >= 0 &&
          typeof g.__nfuMetricsCookies === 'function') {
        try {
          // Live ticket when the real server already answered this page: a
          // replayed 3PSIDMCPP is rejected with (4215) onAuth failed.
          var _lb = (typeof g.__nfuMcookLive === 'string') ? g.__nfuMcookLive : '';
          var _mc = String(_lb ? g.__nfuMetricsCookies(_lb) : g.__nfuMetricsCookies()) || '';
          if (_mc.length > 40 && _mc.charAt(0) === '{') {
            var _bs = String(body);
            var _k = _bs.indexOf('"metricsCookies":');
            var _ob = _bs.indexOf('{', _k);
            if (_ob > 0) {
              var _dep = 0, _cl = -1;
              for (var _p = _ob; _p < _bs.length; _p++) {
                var _ch = _bs.charAt(_p);
                if (_ch === '{') _dep++;
                else if (_ch === '}') { _dep--; if (_dep === 0) { _cl = _p; break; } }
              }
              if (_cl > 0) {
                body = _bs.slice(0, _ob) + _mc + _bs.slice(_cl + 1);
                print('[MCOOK-INJ] applied chars=' + _mc.length);
              }
            }
          }
        } catch(mi1){}
      }
      // The room join carries the same analytics identity under a different key
      // (generalCookies). A stale bundle makes the room server answer 4045
      // ("Invalid onJoin arguments") before the handshake, so swap it wholesale.
      if (body && body.indexOf('"generalCookies":') >= 0 && typeof g.__nfuGenCook === 'function') {
        try {
          var _gc = String(g.__nfuGenCook() || '');
          // 3PSIDMCPP is a short-lived ticket minted per visit, so the captured
          // bundle only contributes the long-lived fields (3PSIDMC, 3PSIDMCSP,
          // trafficCode, compliance) and the ticket is overlaid from this run's
          // own /index/metrics/cookies answer. Keeping both halves from one
          // identity keeps the server's signature check consistent. The live
          // answer captured at the network layer wins over the on-disk copy.
          if (_gc.length > 40) {
            try {
              var _mb = '';
              if (typeof g.__nfuMcookLive === 'string' && g.__nfuMcookLive.length > 40) {
                _mb = g.__nfuMcookLive;
              } else if (typeof g.__nfuMetricsBody === 'function') {
                _mb = String(g.__nfuMetricsBody() || '');
              }
              var _mp = _mb.indexOf('"3PSIDMCPP":"');
              if (_mp > 0) {
                _mp += 13;
                var _me = _mb.indexOf('"', _mp);
                if (_me > _mp) {
                  var _fresh = _mb.slice(_mp, _me);
                  _gc = _gc.replace(/"3PSIDMCPP":"[^"]*"/, '"3PSIDMCPP":"' + _fresh + '"');
                  print('[GENCOOK-PP] live chars=' + _fresh.length);
                }
              } else {
                print('[GENCOOK-PP] no live ticket in metrics answer');
              }
            } catch(gi0){}
          }
          if (_gc.length > 40 && _gc.charAt(0) === '{') {
            var _gs = String(body), _gk = _gs.indexOf('"generalCookies":');
            var _go = _gs.indexOf('{', _gk);
            if (_go > 0) {
              var _gd = 0, _gcl = -1;
              for (var _gp = _go; _gp < _gs.length; _gp++) {
                var _gch = _gs.charAt(_gp);
                if (_gch === '{') _gd++;
                else if (_gch === '}') { _gd--; if (_gd === 0) { _gcl = _gp; break; } }
              }
              if (_gcl > 0) { body = _gs.slice(0, _go) + _gc + _gs.slice(_gcl + 1); print('[GENCOOK-INJ] chars=' + _gc.length); }
            }
          }
        } catch(gi1){}
      }
      if (/\/social\//.test(url) && body && body.indexOf('"3PSIDMCSP":"') >= 0) {
        try {
          var _sp = (typeof g.__nfuMcsp === 'function') ? String(g.__nfuMcsp() || '') : '';
          if (_sp.length > 8) {
            body = String(body).replace(/"3PSIDMCSP":"[^"]*"/, '"3PSIDMCSP":"' + _sp + '"');
            print('[MCSP] applied chars=' + _sp.length);
          }
        } catch(mp1){}
      }
      if (/\/social\/bloxd-matchmake/.test(url)) {
        try {
          var _mb = String(body || '');
          print('[MMLEN] ' + _mb.length);
          for (var _mi = 0; _mi < _mb.length && _mi < 2000; _mi += 170) {
            print('[MMB' + (_mi / 170) + '] ' + _mb.slice(_mi, _mi + 170));
          }
        } catch(mb1){}
      }
      // A stock fetch POST with a string body always carries
      // "Content-Type: text/plain;charset=UTF-8" and a cross-origin Origin header,
      // added by the network layer rather than by the page. bloxd's requests rely on
      // that implicit pair; without Content-Type the game-server endpoints simply do
      // not answer (the request hangs until our receive timeout reports status 0),
      // which is exactly what the keep-alive loop kept doing.
      if (method !== 'GET' && !/(^|\r?\n)[ \t]*content-type[ \t]*:/i.test(hstr || '')) hstr = (hstr || '') + 'Content-Type: text/plain;charset=UTF-8\r\n';
      if (!/(^|\r?\n)[ \t]*origin[ \t]*:/i.test(hstr || '')) { var _org = 'https://' + _docHost(); if (_org !== 'https://') hstr = (hstr || '') + 'Origin: ' + _org + '\r\n'; }
      // bloxd's own client marks every cross-origin API call with this header -
      // spelled with three t's in the site's source, so the typo is the contract.
      // The server answers a bare 401 to a request without it, which is what kept
      // matchmake and the social endpoints unauthenticated even with a valid jar.
      if (!/(^|\r?\n)[ \t]*x-requested-with[ \t]*:/i.test(hstr || '')) {
        var _xh = _hostOf(url);
        if (_xh && _xh !== _docHost()) hstr = (hstr || '') + 'X-Requested-With: XMLHtttpRequest\r\n';
      }
      var r = g.__nfuHttpRequest(method, url, (body || ''), hstr || '');
      // The origin (and its Cloudflare edge) occasionally drops a burst of
      // parallel requests during boot - traffic-code, social info and matchmake
      // all came back with status 0 in the same tick, and bloxd answered with
      // "Request to matchmaking failed: Error: network" and bounced to the menu.
      // One immediate retry per call is what a browser does anyway.
      if (!r || (typeof r === 'object' && (r.status | 0) === 0 && method === 'GET')) {
        try { print('[REQ-RETRY] ' + String(url).slice(0, 120)); } catch(rp){}
        try { r = g.__nfuHttpRequest(method, url, (body || ''), hstr || ''); } catch(rq){}
      }
      if (r && typeof r === 'object') { _jarAdopt(url, r.setCookie); if (/\/index\/metrics\/cookies(\?|$)/.test(String(url))) _jarAdoptBody(url, r.body); r = _preflightMatchmake(r, method, url, body, hstr); return r; }
      if (typeof r === 'string') return { status: 200, body: r };
    } catch(e){ try { print('[XHR-FAIL] ' + url + ' :: ' + (e && e.message ? e.message : e)); } catch(pe){} }
    return null;
  }
  // Absolute-URL helper for the request path (the base shim's nfuResolveUrl
  // lives in a different closure and is not reachable from the patch).
  function _absUrl(u){
    try {
      if (/^https?:\/\//i.test(u) || u.indexOf('data:') === 0) return u;
      var base = (g.location && g.location.href) || '';
      if (!base) return u;
      if (u.charAt(0) === '/') return base.replace(/^(https?:\/\/[^\/]+).*/, '$1') + u;
      return base.replace(/[^\/]*$/, '') + u;
    } catch(e){}
    return u;
  }
  // Binary upload bodies (ArrayBuffer / typed-array views) must reach the
  // native binding untouched - String(b) would send "1,2,3" instead of bytes.
  function _binBody(b){
    try {
      if (typeof ArrayBuffer === 'undefined') return null;
      if (b instanceof ArrayBuffer) return b;
      if (ArrayBuffer.isView && ArrayBuffer.isView(b)) {
        if (b.byteOffset === 0 && b.byteLength === b.buffer.byteLength) return b.buffer;
        return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
      }
    } catch(e){}
    return null;
  }
  // ---- byte helpers ----------------------------------------------------
  // Everything that crosses the native boundary is UTF-8; this is the only
  // place the conversion lives so Body/Blob/FormData all agree on it.
  function _strBytes(s){
    try { if (g.TextEncoder) return new g.TextEncoder().encode(String(s == null ? '' : s)); } catch(e){}
    var t = String(s == null ? '' : s), out = [], i, c;
    for (i = 0; i < t.length; i++){
      c = t.charCodeAt(i);
      if (c < 0x80) out.push(c);
      else if (c < 0x800) out.push(0xC0 | (c >> 6), 0x80 | (c & 63));
      else out.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    }
    return new Uint8Array(out);
  }
  function _bytesStr(u8){
    try { if (g.TextDecoder) return new g.TextDecoder('utf-8').decode(u8); } catch(e){}
    var s = '';
    for (var i = 0; i < u8.length; i++) s += String.fromCharCode(u8[i]);
    return s;
  }
  // Uint8Array -> ArrayBuffer with no slack (a subarray shares a bigger buffer,
  // and the native binding takes an exact ArrayBuffer).
  function _exactBuf(u8){
    if (u8.byteOffset === 0 && u8.byteLength === u8.buffer.byteLength) return u8.buffer;
    return new Uint8Array(u8).buffer;
  }
  function _toBytes(x){
    if (x == null) return new Uint8Array(0);
    if (x instanceof NfuBlob) return x._bytes();
    if (typeof x === 'string') return _strBytes(x);
    try { if (x instanceof ArrayBuffer) return new Uint8Array(x); } catch(e){}
    try { if (typeof ArrayBuffer !== 'undefined' && ArrayBuffer.isView && ArrayBuffer.isView(x)) return new Uint8Array(x.buffer, x.byteOffset, x.byteLength); } catch(e){}
    return _strBytes(String(x));
  }
  var _REASON = { 200:'OK', 201:'Created', 202:'Accepted', 204:'No Content', 206:'Partial Content',
                  301:'Moved Permanently', 302:'Found', 303:'See Other', 304:'Not Modified',
                  307:'Temporary Redirect', 308:'Permanent Redirect', 400:'Bad Request',
                  401:'Unauthorized', 402:'Payment Required', 403:'Forbidden', 404:'Not Found',
                  405:'Method Not Allowed', 406:'Not Acceptable', 409:'Conflict', 410:'Gone',
                  413:'Payload Too Large', 415:'Unsupported Media Type', 422:'Unprocessable Entity',
                  429:'Too Many Requests', 500:'Internal Server Error', 501:'Not Implemented',
                  502:'Bad Gateway', 503:'Service Unavailable', 504:'Gateway Timeout' };
  // ---- Headers ---------------------------------------------------------
  function NfuHeaders(init){
    this._m = {};
    this._o = {};                     // lowercase -> name as first received
    if (init == null) return;
    var self = this, k;
    if (typeof init === 'object' && typeof init.forEach === 'function' && !(init instanceof Array)){
      init.forEach(function(v, n){ self.append(n, v); });          // Headers or Map
    } else if (init instanceof Array){
      for (k = 0; k < init.length; k++) if (init[k]) this.append(init[k][0], init[k][1]);
    } else if (typeof init === 'object'){
      for (k in init) if (Object.prototype.hasOwnProperty.call(init, k)) this.append(k, init[k]);
    }
  }
  function _hOwn(o, k){ return Object.prototype.hasOwnProperty.call(o, k); }
  NfuHeaders.prototype._n = function(n){ return String(n == null ? '' : n).toLowerCase(); };
  NfuHeaders.prototype.append = function(n, v){
    var k = this._n(n), s = String(v == null ? '' : v).replace(/^[ \t]+|[ \t]+$/g, '');
    if (this._o[k] === undefined) this._o[k] = String(n == null ? '' : n);
    this._m[k] = (this._m[k] === undefined) ? s : (this._m[k] + ', ' + s);
  };
  NfuHeaders.prototype.set = function(n, v){
    var k = this._n(n);
    this._o[k] = String(n == null ? '' : n);
    this._m[k] = String(v == null ? '' : v).replace(/^[ \t]+|[ \t]+$/g, '');
  };
  NfuHeaders.prototype.get = function(n){ var v = this._m[this._n(n)]; return (v === undefined) ? null : v; };
  NfuHeaders.prototype.has = function(n){ return this._m[this._n(n)] !== undefined; };
  NfuHeaders.prototype['delete'] = function(n){
    var k = this._n(n);
    if (this._m[k] !== undefined) delete this._m[k];
    if (this._o[k] !== undefined) delete this._o[k];
  };
  NfuHeaders.prototype.forEach = function(cb, ta){
    for (var k in this._m) if (_hOwn(this._m, k)) cb.call(ta, this._m[k], k, this);
  };
  NfuHeaders.prototype.keys = function(){ var a = [], k; for (k in this._m) if (_hOwn(this._m, k)) a.push(k); return a; };
  NfuHeaders.prototype.values = function(){ var a = [], k; for (k in this._m) if (_hOwn(this._m, k)) a.push(this._m[k]); return a; };
  NfuHeaders.prototype.entries = function(){ var a = [], k; for (k in this._m) if (_hOwn(this._m, k)) a.push([k, this._m[k]]); return a; };
  try { if (typeof Symbol !== 'undefined' && Symbol.iterator) NfuHeaders.prototype[Symbol.iterator] = NfuHeaders.prototype.entries; } catch(e){}
  // Native header block ("Name: value" per '\n') -> Headers.
  function _hdrParse(raw){
    var h = new NfuHeaders(), lines = String(raw == null ? '' : raw).split('\n');
    for (var i = 0; i < lines.length; i++){
      var ln = lines[i], c = ln.indexOf(':');
      if (c > 0) h.append(ln.slice(0, c), ln.slice(c + 1));
    }
    return h;
  }
  function _hdrToStr(h){
    var s = '';
    if (!h) return s;
    try {
      if (typeof h.forEach === 'function'){ h.forEach(function(v, k){ s += k + ': ' + v + '\r\n'; }); return s; }
      if (h instanceof Array){ for (var i = 0; i < h.length; i++) s += h[i][0] + ': ' + h[i][1] + '\r\n'; return s; }
      for (var k in h) if (_hOwn(h, k)) s += k + ': ' + h[k] + '\r\n';
    } catch(e){}
    return s;
  }
  function _hdrStrAdd(s, name, val){
    var lines = String(s || '').split('\n');
    for (var i = 0; i < lines.length; i++){
      var c = lines[i].indexOf(':');
      if (c > 0 && lines[i].slice(0, c).replace(/^[ \t]+|[ \t]+$/g, '').toLowerCase() === name.toLowerCase()) return s;
    }
    return (s || '') + name + ': ' + val + '\r\n';
  }
  function _hdrAdd(obj, name, val){
    for (var k in obj) if (_hOwn(obj, k) && String(k).toLowerCase() === name.toLowerCase()) return;
    obj[name] = val;
  }
  // ---- Blob / File -----------------------------------------------------
  function NfuBlob(parts, opts){
    this._p = [];
    // Array.isArray (not instanceof) so a parts array from another realm still
    // counts: instanceof only matches this context's Array constructor.
    if (typeof Array.isArray === 'function' ? Array.isArray(parts) : (parts instanceof Array))
      for (var i = 0; i < parts.length; i++) this._p.push(parts[i]);
    this.type = (opts && opts.type) ? String(opts.type).toLowerCase() : '';
    this._b = null;
  }
  try { Object.defineProperty(NfuBlob.prototype, 'size', { get: function(){ return this._bytes().byteLength; }, configurable: true }); } catch(e){}
  NfuBlob.prototype._bytes = function(){
    if (this._b) return this._b;
    var i, c, chunks = [], total = 0;
    for (i = 0; i < this._p.length; i++){
      c = _toBytes(this._p[i]);
      if (c.byteLength) { chunks.push(c); total += c.byteLength; }
    }
    var out = new Uint8Array(total), off = 0;
    for (i = 0; i < chunks.length; i++){ out.set(chunks[i], off); off += chunks[i].byteLength; }
    this._b = out;
    return out;
  };
  NfuBlob.prototype.arrayBuffer = function(){ return Promise.resolve(_exactBuf(this._bytes())); };
  NfuBlob.prototype.text = function(){ return Promise.resolve(_bytesStr(this._bytes())); };
  NfuBlob.prototype.slice = function(a, b, t){
    var u = this._bytes(), n = u.byteLength;
    var s = (a === undefined) ? 0 : (a < 0 ? Math.max(n + a, 0) : Math.min(a, n));
    var e = (b === undefined) ? n : (b < 0 ? Math.max(n + b, 0) : Math.min(b, n));
    if (e < s) e = s;
    return new NfuBlob([u.subarray(s, e)], { type: t || this.type });
  };
  function NfuFile(parts, name, opts){
    var b = new NfuBlob(parts, opts);
    b.name = String(name == null ? '' : name);
    b.lastModified = Date.now ? Date.now() : 0;
    return b;
  }
  // ---- FormData --------------------------------------------------------
  function NfuFormData(){ this._e = []; }
  NfuFormData.prototype.append = function(n, v){ this._e.push([String(n), v]); };
  NfuFormData.prototype.set = function(n, v){
    n = String(n); var hit = false;
    for (var i = 0; i < this._e.length; i++){
      if (this._e[i][0] !== n) continue;
      if (!hit) { this._e[i][1] = v; hit = true; } else { this._e.splice(i, 1); i--; }
    }
    if (!hit) this.append(n, v);
  };
  NfuFormData.prototype.get = function(n){
    n = String(n);
    for (var i = 0; i < this._e.length; i++) if (this._e[i][0] === n) return this._e[i][1];
    return null;
  };
  NfuFormData.prototype.getAll = function(n){
    n = String(n); var a = [];
    for (var i = 0; i < this._e.length; i++) if (this._e[i][0] === n) a.push(this._e[i][1]);
    return a;
  };
  NfuFormData.prototype.has = function(n){ return this.get(n) !== null; };
  NfuFormData.prototype['delete'] = function(n){
    n = String(n);
    for (var i = 0; i < this._e.length; i++) if (this._e[i][0] === n) { this._e.splice(i, 1); i--; }
  };
  NfuFormData.prototype.forEach = function(cb, ta){
    for (var i = 0; i < this._e.length; i++) cb.call(ta, this._e[i][1], this._e[i][0], this);
  };
  NfuFormData.prototype.entries = function(){ var a = []; for (var i = 0; i < this._e.length; i++) a.push([this._e[i][0], this._e[i][1]]); return a; };
  NfuFormData.prototype.keys = function(){ var a = []; for (var i = 0; i < this._e.length; i++) a.push(this._e[i][0]); return a; };
  NfuFormData.prototype.values = function(){ var a = []; for (var i = 0; i < this._e.length; i++) a.push(this._e[i][1]); return a; };
  var _mpSeq = 0;
  // FormData -> multipart bytes. The body goes to the native binding as an
  // ArrayBuffer (not a string) so file parts survive byte-for-byte.
  function _multipart(fd){
    var bd = '----nefuFormBoundary' + (++_mpSeq) + 'x' + String(Math.floor(Math.random() * 1000000000));
    var chunks = [], total = 0, i;
    function push(u8){ if (u8 && u8.byteLength) { chunks.push(u8); total += u8.byteLength; } }
    function clean(s){ return String(s == null ? '' : s).replace(/"/g, '%22').replace(/[\r\n]/g, ' '); }
    for (i = 0; i < fd._e.length; i++){
      var name = clean(fd._e[i][0]), val = fd._e[i][1];
      var head = '--' + bd + '\r\nContent-Disposition: form-data; name="' + name + '"';
      if (val instanceof NfuBlob){
        head += '; filename="' + clean(val.name || 'blob') + '"\r\nContent-Type: ' +
                (val.type || 'application/octet-stream') + '\r\n\r\n';
        push(_strBytes(head)); push(val._bytes());
      } else {
        push(_strBytes(head + '\r\n\r\n' + String(val)));
      }
      push(_strBytes('\r\n'));
    }
    push(_strBytes('--' + bd + '--\r\n'));
    var out = new Uint8Array(total), off = 0;
    for (i = 0; i < chunks.length; i++){ out.set(chunks[i], off); off += chunks[i].byteLength; }
    return { body: out.buffer, type: 'multipart/form-data; boundary=' + bd };
  }
  // Request body -> {body (string|ArrayBuffer), type (auto content-type)}
  function _encodeBody(b){
    if (b === undefined || b === null) return { body: '', type: '' };
    if (b instanceof NfuFormData) return _multipart(b);
    if (b instanceof NfuBlob) return { body: _exactBuf(b._bytes()), type: b.type || '' };
    if (typeof b === 'string') return { body: b, type: '' };
    try {
      if (typeof URLSearchParams !== 'undefined' && b instanceof URLSearchParams)
        return { body: String(b), type: 'application/x-www-form-urlencoded;charset=UTF-8' };
    } catch(e){}
    var bin = _binBody(b);
    if (bin !== null) return { body: bin, type: '' };
    if (typeof ArrayBuffer !== 'undefined' && b instanceof ArrayBuffer) return { body: b, type: '' };
    if (typeof b === 'object'){
      try { return { body: JSON.stringify(b), type: 'application/json' }; } catch(e){}
    }
    return { body: String(b), type: '' };
  }
  // ---- Response --------------------------------------------------------
  function NfuResponse(url, status, hdrs, text, ab){
    this.url = String(url || '');
    this.status = status || 0;
    this.statusText = _REASON[this.status] || '';
    this.ok = (this.status >= 200 && this.status < 300);
    this.headers = hdrs || new NfuHeaders();
    this.redirected = false; this.type = 'basic'; this.bodyUsed = false;
    this._t = (text == null) ? '' : String(text);
    this._ab = ab || null;
  }
  NfuResponse.prototype.text = function(){ this.bodyUsed = true; return Promise.resolve(this._t); };
  function _abText(ab){
    // Recover text from a body that arrived through the binary path. bloxd posts
    // to /images/<Block>.png, the origin labels the reply as an image type, so the
    // payload is delivered as an ArrayBuffer - and the caller then reads it as
    // JSON. Decoding it here keeps that read working instead of rejecting on an
    // empty string.
    try {
      if (typeof TextDecoder !== 'undefined') { return new TextDecoder().decode(new Uint8Array(ab)); }
    } catch(e){}
    try {
      var u8 = new Uint8Array(ab), s = '';
      for (var i = 0; i < u8.length; i++) s += String.fromCharCode(u8[i]);
      return s;
    } catch(e2){}
    return '';
  }
  NfuResponse.prototype.json = function(){
    this.bodyUsed = true;
    var t = this._t;
    if ((t === '' || t == null) && this._ab) t = _abText(this._ab);
    if (t === '' || t == null) {
      // An empty answer carries no JSON. Resolving null (rather than rejecting on
      // JSON.parse('')) matches what a caller can usefully act on and stops the
      // unhandled rejection from aborting the game's setup chain.
      try { print('[RJSON-EMPTY] ' + String(this.url || '')); } catch(rj){}
      return Promise.resolve(null);
    }
    try { return Promise.resolve(JSON.parse(t)); } catch(e){
      // The origin sometimes answers a JSON call with an image (bloxd posts to
      // /images/<Block>.png with Accept: application/json and still gets a PNG
      // back). Rejecting there aborts the game's setup chain over a payload it
      // only treats as optional, so hand back null and let it continue.
      try { print('[RJSON-FAIL] ' + String(this.url || '') + ' head=' + String(t).slice(0, 60)); } catch(rf){}
      return Promise.resolve(null); }
  };
  NfuResponse.prototype.arrayBuffer = function(){
    this.bodyUsed = true;
    return Promise.resolve(this._ab ? this._ab : _exactBuf(_strBytes(this._t)));
  };
  NfuResponse.prototype.blob = function(){
    this.bodyUsed = true;
    var src = this._ab ? new Uint8Array(this._ab) : _strBytes(this._t);
    return Promise.resolve(new NfuBlob([src], { type: this.headers.get('content-type') || '' }));
  };
  // response.body: a minimal ReadableStream over the whole payload. The
  // fetch pump materialises the body before the Response exists, so a single
  // read() hands out every byte and the next reports done.
  try {
    Object.defineProperty(NfuResponse.prototype, 'body', {
      get: function(){
        var self = this;
        return {
          getReader: function(){
            var done = false;
            var rd = {
              read: function(){
                if (done) return Promise.resolve({ done: true, value: undefined });
                done = true;
                self.bodyUsed = true;
                var u8 = self._ab ? new Uint8Array(self._ab) : _strBytes(self._t);
                return Promise.resolve({ done: false, value: u8 });
              },
              cancel: function(){ done = true; return Promise.resolve(); },
              releaseLock: function(){},
              close: function(){ done = true; return Promise.resolve(); }
            };
            // bloxd's wasm downloader consumes the body with
            // `for await (const chunk of response.body.getReader())`, so the
            // reader has to be async-iterable. Without Symbol.asyncIterator
            // the whole boot dies with "not a function (got undefined)" and
            // the module never reaches WebAssembly.
            if (typeof Symbol !== 'undefined' && Symbol.asyncIterator){
              rd[Symbol.asyncIterator] = function(){ return rd; };
              rd.next = function(){ return rd.read(); };
            }
            return rd;
          },
          cancel: function(){ self.bodyUsed = true; return Promise.resolve(); },
          locked: false
        };
      },
      configurable: true
    });
  } catch(e){}
  NfuResponse.prototype.clone = function(){
    return new NfuResponse(this.url, this.status, new NfuHeaders(this.headers.entries()), this._t, this._ab);
  };
  // ---- XMLHttpRequest (full method/body/headers) ----
  function NfuXHR(){
    this.readyState = 0; this.status = 0; this.statusText = '';
    this.responseText = ''; this.response = null; this.responseType = '';
    this.responseURL = ''; this.withCredentials = false;
    this._url = ''; this._method = 'GET'; this._body = ''; this._reqHeaders = {};
    this._raw = ''; this._hdrs = new NfuHeaders(); this._ab = null;
    this.onreadystatechange = null; this.onload = null; this.onerror = null;
    this.onprogress = null; this.onloadend = null; this.onabort = null; this.ontimeout = null;
    this.upload = { addEventListener: function(){}, removeEventListener: function(){} };
  }
  NfuXHR.prototype.open = function(m, u){
    this._method = String(m || 'GET').toUpperCase();
    this._url = String(u);
    this._body = ''; this._reqHeaders = {};
    this.readyState = 1;
  };
  NfuXHR.prototype.setRequestHeader = function(k, v){ (this._reqHeaders = this._reqHeaders || {})[k] = v; };
  NfuXHR.prototype.getResponseHeader = function(n){ return this._hdrs ? this._hdrs.get(n) : null; };
  NfuXHR.prototype.getAllResponseHeaders = function(){
    // Mirror the browser: one "name: value\r\n" line per header, '' when none.
    // Names come back as the server spelled them (Headers itself lowercases).
    var m = this._hdrs ? this._hdrs.entries() : [], s = '';
    for (var i = 0; i < m.length; i++) s += (this._hdrs._o[m[i][0]] || m[i][0]) + ': ' + m[i][1] + '\r\n';
    return s;
  };
  NfuXHR.prototype.overrideMimeType = function(){};
  NfuXHR.prototype.abort = function(){};
  NfuXHR.prototype.addEventListener = function(t, f){ var k = 'on' + t; if (!this[k]) this[k] = f; };
  NfuXHR.prototype.removeEventListener = function(){};
  NfuXHR.prototype.send = function(b){
    var self = this;
    if (b !== undefined && b !== null){
      var eb = _encodeBody(b);
      self._body = eb.body;
      if (eb.type) _hdrAdd(self._reqHeaders = self._reqHeaders || {}, 'Content-Type', eb.type);
    }
    (g.__nfuTimers = g.__nfuTimers || []).push(function(){
      var t = '', st = 200, raw = '', ab = null;
      var url = self._url || '';
      try { if (String(url).indexOf('/auth') >= 0) print('[XHRO] ' + self._method + ' ' + url); } catch(e){}
      if (/index\/metrics\/cookies/.test(url)) {
        // We answer this locally, so the real request shape is invisible. Log
        // it once: the body carries the signed cookie-metrics object and the
        // server rejects a request without it ("Request rejected"), which is
        // the reason bloxd's identity chain cannot be reproduced offline.
        try {
          if (!g.__nfuMcookSeen) {
            g.__nfuMcookSeen = 1;
            print('[MCOOK] ' + self._method + ' ' + url + ' body=' + String(self._body || '').slice(0, 400)
                  + ' hdrs=' + String(_buildHeaders(self._reqHeaders) || '').replace(/\r?\n/g, '|').slice(0, 300));
          }
        } catch(pe){}
        t = _identityJson();                                         // never transmit telemetry
      } else {
        if (url.indexOf('joinOrCreate') >= 0 && url.indexOf('game-server') >= 0) try {
          var _mb = String(self._body || '');
          print('[MMREQ] ' + self._method + ' blen=' + _mb.length + ' hdr=' + String(_buildHeaders(self._reqHeaders) || '').replace(/\r?\n/g, '|'));
          for (var _mi = 0; _mi < _mb.length; _mi += 200) print('[MMB' + (_mi / 200) + '] ' + _mb.slice(_mi, _mi + 200));
        } catch(me){}
        var r = _doReq(self._method, url, self._body, _buildHeaders(self._reqHeaders));
        if (r){
          st = r.status || 0;
          t = (r.body != null) ? String(r.body) : '';
          raw = r.hdr || '';
          ab = r.bodyAB || null;
        }
        if (url.indexOf('joinOrCreate') >= 0 && url.indexOf('game-server') >= 0) try {
          var _rb = String(t || '');
          print('[MMRESP] st=' + st + ' blen=' + _rb.length);
          for (var _rj = 0; _rj < _rb.length; _rj += 200) print('[MMRB' + (_rj / 200) + '] ' + _rb.slice(_rj, _rj + 200));
        } catch(me3){}
        // offline fallback: emulate an anon identity so the app reaches menu
        if ((!t || st >= 400) && /auth\/(anonymous|userdata|login)/.test(url)) {
          t = _identityJson();
          st = 200;
        }
        if (!t && !ab && (!st || st >= 400)) st = 0;
      }
      self._raw = raw;
      self._hdrs = _hdrParse(raw);
      self.responseURL = url;
      self.readyState = 4; self.status = st; self.statusText = _REASON[st] || '';
      self.responseText = t;
      self._ab = ab || _exactBuf(_strBytes(t));
      var rt = self.responseType || '';
      if (rt === 'arraybuffer') self.response = self._ab;
      else if (rt === 'blob') self.response = new NfuBlob([new Uint8Array(self._ab)], { type: self._hdrs.get('content-type') || '' });
      else if (rt === 'json') { try { self.response = JSON.parse(t); } catch(e){ self.response = null; } }
      else if (rt === 'document') self.response = null;
      else self.response = t;
      // Surface non-2xx: a rejected auth/matchmake otherwise shows up only as a
      // generic "Can't connect", with no hint about which request failed.
      if (st < 200 || st >= 300){
        g.__nfuXhrErrN = (g.__nfuXhrErrN || 0) + 1;
        if (g.__nfuXhrErrN <= 24) try { print('[XHR] ' + st + ' ' + self._method + ' ' + url + ' len=' + t.length); } catch(e){}
      }
      if (self.onreadystatechange) try { self.onreadystatechange(); } catch(e){}
      if (self.onload) try { self.onload(); } catch(e){}
      else if (self.onerror && (st === 0)) try { self.onerror(); } catch(e){}
      if (self.onloadend) try { self.onloadend(); } catch(e){}
    });
  };
  g.XMLHttpRequest = NfuXHR;
  // ---- fetch (forward method/body/headers, real Response) ----
  g.fetch = function(u, o){
    var url = String(u), method = 'GET', body = '', hstr = '';
    if (/\.wasm(\?|$)/.test(url)) {
      var okeys = '';
      try { okeys = (o && typeof o === 'object') ? Object.keys(o).join(',') : String(typeof o); } catch(oe){}
      try { print('[WFETCH-IN] ' + url.slice(-40) + ' opt=' + okeys); } catch(pe){}
    }
    try {
      if (o && typeof o === 'object'){
        method = String(o.method || 'GET').toUpperCase();
        var eb = _encodeBody(o.body);
        body = eb.body;
        hstr = _hdrToStr(o.headers);
        if (eb.type) hstr = _hdrStrAdd(hstr, 'Content-Type', eb.type);
      }
    } catch(e){}
    return new Promise(function(res, rej){
      (g.__nfuTimers = g.__nfuTimers || []).push(function(){
        var r = null;
        // Telemetry flush and the identity endpoints are answered locally, the
        // same way NfuXHR does: bloxd gates its whole anonymous sign-in chain
        // behind "POST /index/metrics/cookies must return 2xx", and a network
        // failure there ("BadHttpCodeError: 0") left the app stuck on the
        // loading overlay forever.
        if (/index\/metrics\/cookies/.test(url) && !g.__nfuMcookReal) {
          // The captured identity blob wins: it is the only answer the server
          // accepts as this visitor's account, and every later social call is
          // validated against it. Only when no capture exists does the engine
          // try the honest request, and only then fall back to a stub.
          var mb = '';
          try { mb = (typeof g.__nfuMetricsBody === 'function') ? String(g.__nfuMetricsBody() || '') : ''; } catch(me){ mb = ''; }
          if (mb) {
            try { print('[MCOOK] served captured identity chars=' + mb.length); } catch(pe){}
            r = { status: 200, hdr: 'content-type: application/json\r\n', body: mb, bodyAB: null };
            g.__nfuMcookReal = 1;
          } else {
          // Try the honest request first: a real 2xx here carries the signed
          // anonymous identity that every later social/matchmake call is
          // validated against, so fabricating it guarantees 401s downstream.
          var rr = _doReq(method, url, body, hstr);
          var rstat = (rr && rr.status) ? (rr.status | 0) : 0;
          try { print('[MCOOK] real status=' + rstat + ' bodyLen=' + ((rr && rr.body) ? String(rr.body).length : 0) + ' setCookie=' + JSON.stringify((rr && rr.setCookie) ? String(rr.setCookie) : '') + ' body=' + String((rr && rr.body) ? rr.body : '').slice(0, 220)); } catch(pe){}
          if (rr && rstat >= 200 && rstat < 300 && rr.body) {
            r = rr; g.__nfuMcookReal = 1;
            // Keep the freshly minted blob: 3PSIDMCPP is what the room server
            // validates the join body against, and it is short-lived.
            try { if (String(rr.body).charAt(0) === '{') { g.__nfuMcookLive = String(rr.body); print('[MCOOK] live identity chars=' + g.__nfuMcookLive.length); } } catch(lb1){}
          }
          else { r = { status: 200, hdr: 'content-type: application/json\r\n', body: _identityJson(), bodyAB: null }; }
          }
        } else if (/index\/metrics\/cookies/.test(url)) {
          r = { status: 200, hdr: 'content-type: application/json\r\n', body: _identityJson(), bodyAB: null };
        } else if (/\.wasm(\?|$)/.test(url) && typeof g.__nfuLocalWasm === 'function') {
          // Serve wasm modules from disk when a copy exists. Both the Rust
          // engine module and Babylon's Draco decoder are fetched while the
          // join chain is already running, and a network fetch there blocks the
          // tick for 18-22 s - long enough for bloxd's Colyseus seat
          // reservation to expire ("seat reservation expired").
          var wp = '', wab = null;
          try { wp = String(g.__nfuLocalWasm(String(url)) || ''); } catch(we){ wp = ''; }
          if (wp && /draco_decoder/.test(url)) {
            // Same directory, decoder file name as shipped next to the engine
            // module cache.
            try { wp = wp.replace(/[^\\\/]*$/, 'draco_decoder.wasm'); } catch(wr){}
          }
          if (wp) {
            try { wab = g.__nfuReadFile(wp); } catch(we2){ wab = null; }
          }
          if (wab && wab.byteLength) {
            r = { status: 200, hdr: 'content-type: application/wasm\r\n', body: '', bodyAB: wab };
          } else {
            r = _doReq(method, url, body, hstr);
          }
        } else {
          r = _doReq(method, url, body, hstr);
          // bloxd asks the game server for block textures with a POST
          // (`POST /images/Andesite.png`). That route answers GET only: the
          // POST hangs until the receive timeout and comes back status 0, which
          // stalls the JS thread for seconds per texture and leaves the world
          // without its block atlas. Retry the same URL as a plain GET.
          if (/game-server/.test(url)) { g.__nfuHdrN = (g.__nfuHdrN || 0) + 1; if (g.__nfuHdrN <= 12) try { print('[HDR] ' + method + ' ' + url.slice(-38) + ' hdr=' + hstr.slice(0, 320) + ' status=' + ((r && r.status) | 0)); } catch(pe){} }
          if (r && (r.status | 0) === 0 && method !== 'GET' &&
              /\.(png|jpg|jpeg|webp|gif|ico|bmp|ktx|ktx2)([?#]|$)/i.test(url)) {
            try { print('[FETCH-GETRETRY] ' + url); } catch(pe){}
            r = _doReq('GET', url, null, hstr);
          }
          if (r && ((!r.body && !r.bodyAB) || (r.status || 0) >= 400) &&
              /auth\/(anonymous|userdata|login)/.test(url)) {
            r = { status: 200, hdr: 'content-type: application/json\r\n', body: _identityJson(), bodyAB: null };
          }
        }
        if (!r){ try { print('[FETCH-FAIL] ' + url + ' nat=' + (typeof g.__nfuHttpRequest) + ' win=' + (typeof window) + ' same=' + (typeof window === 'object' && window === g) + ' rowh=' + (typeof __nfuHttpRequest)); } catch(pe){} rej(new Error('network')); return; }
        var st = r.status || 0, t = (r.body != null) ? String(r.body) : '';
        var ab = r.bodyAB || null;
        if (!t && !ab) { try { print('[FETCH-EMPTY] ' + url); } catch(pe){} }
        if (/\.wasm(\?|$)/.test(String(url))) {
          try { print('[WASMFETCH] st=' + st + ' ab=' + (ab ? ab.byteLength : -1) + ' t=' + t.length); } catch(pe){}
          // Echo every property bloxd touches on the wasm Response: the boot
          // still dies with a bare "not a function" after the bytes arrive,
          // so the only way to name the missing surface is to watch it.
          try {
            var _rawRes = new NfuResponse(url, st, _hdrParse(r.hdr || ''), t, ab);
            res(new Proxy(_rawRes, {
              get: function(tg, k){
                try { print('[WPROP] ' + String(k)); } catch(pe){}
                var vv = tg[k];
                if (typeof vv === 'function') return function(){ return vv.apply(tg, arguments); };
                return vv;
              }
            }));
            return;
          } catch(pe){ try { print('[WPROXY-FAIL] ' + pe); } catch(p2){} }
        }
        // Non-2xx used to be silent here, so bloxd's 401s only showed up as the
        // app's own "BadHttpCodeError" with no URL attached. Log the first few
        // so the failing endpoint is identifiable.
        if (st < 200 || st >= 300){
          g.__nfuFetchErrN = (g.__nfuFetchErrN || 0) + 1;
          if (g.__nfuFetchErrN <= 30) try { print('[FETCH] ' + st + ' ' + method + ' ' + url + ' len=' + t.length); } catch(pe){}
        }
        // Lobby list / starter name decide whether the create-or-join flow can
        // ever put a canvas on screen; a 2xx with an unexpected shape blocks it
        // just as hard as a 401 and prints nothing, so surface the body.
        if (/lobby\/get-new-starter-name|social\/get-open-custom-lobbies|social\/get-favourite-lobbies|social\/bloxd-matchmake|matchmake\/joinOrCreate/.test(url)){
          g.__nfuLobbyDbg = (g.__nfuLobbyDbg || 0) + 1;
          if (g.__nfuLobbyDbg <= 8) try { print('[LOBBY] ' + st + ' ' + url.slice(-40) + ' body=' + String(t).slice(0, 260)); } catch(pe){}
        }
        res(new NfuResponse(url, st, _hdrParse(r.hdr || ''), t, ab));
      });
    });
  };
  // ---- prototype audit ---------------------------------------------------
  // bloxd's wasm-bindgen glue tripped "operand 'prototype' property is not an
  // object" right after instance creation. Every setPrototypeOf whose second
  // operand is not an object is reported here so the culprit is named instead
  // of guessed.
  try {
    var _realSPO = Object.setPrototypeOf;
    Object.setPrototypeOf = function(o, p){
      var pt = typeof p;
      if (p === null || (pt !== 'object' && pt !== 'function')) {
        try {
          print('[SPO-BAD] ' + pt + ' ' + String(p) + ' on ' +
                (o && o.constructor ? (o.constructor.name || '?') : typeof o));
        } catch(pe){}
      }
      return _realSPO(o, p);
    };
  } catch(e){}
  // Expose the web platform constructors the patch emulates. The base shim
  // installs empty stubs for some of them, so these assignments must win.
  function _pub(n, v){ try { g[n] = v; } catch(e){} try { if (typeof window !== 'undefined' && window !== g) window[n] = v; } catch(e){} }
  _pub('Headers', NfuHeaders);
  _pub('Blob', NfuBlob);
  _pub('File', NfuFile);
  _pub('FormData', NfuFormData);
  _pub('Response', NfuResponse);
  _pub('XMLHttpRequest', NfuXHR);
})();
)nfu_patch";

