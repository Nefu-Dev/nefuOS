p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
s = open(p, encoding='utf-8', errors='replace').read()
import re
# tail render: (0,Ij.render)(...document.getElementById("root")))();\n
m = re.search(r'\(0,Ij\.render\)\([^;]*?document\.getElementById\("root"\)\)\)\);\s*$', s)
if not m:
    m = re.search(r'\(0,Ij\.render\)\(.*document\.getElementById\("root"\)\)\}\)\(\);\s*$', s)
print('match:', bool(m))
if m:
    seg = m.group(0)
    print('len', len(seg), 'tail:', repr(seg[-120:]))
    # wrap into setTimeout microtask (skip) -> let main bundle return
    new = 'try { setTimeout(function(){ ' + seg[:-len('})();')] + ' }, 0); } catch(e){}'
    # careful: seg ends with "})();\n" maybe. Rebuild simply:
    inner = seg[:-4] if seg.endswith(')();') else seg
    new = 'try { setTimeout(function(){ ' + inner + ' }, 0); } catch(e){};\n'
    s2 = s[:m.start()] + new
    open(p, 'w', encoding='utf-8', newline='').write(s2)
    print('patched skip-render, size', len(s2))
