# -*- coding: utf-8 -*-
# Insert parseSeed with the exact quoted anchor
p4 = r'D:\mycppos1\nefuOS\core\apps\jsdom_shim.h'
s4 = open(p4, encoding='utf-8').read()
if 'parseSeed(g.__nfuSeed' in s4:
    print('already seeded')
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
    old6 = '''"  var nd = new NfuDoc();\\n"
"  g.document = nd;\\n"'''
    c = s4.count(old6)
    print('anchor count:', c)
    if c == 1:
        s4 = s4.replace(old6, seedjs + old6, 1)
        open(p4, 'w', encoding='utf-8', newline='').write(s4)
        print('shim seeded')
    else:
        raise SystemExit('anchor failed')
