p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
s = open(p, encoding='utf-8', errors='replace').read()
idx = s.rfind('(0,Ij.render)')
print('render idx:', idx)
if idx >= 0:
    seg = s[idx:]
    end = seg.find('})();')
    print('end rel:', end)
    if end >= 0:
        inner = seg[:end+4]
        print('seg tail:', repr(inner[-100:]))
        new = 'try { setTimeout(function(){ ' + inner + ' }, 0); } catch(e){};\n'
        s2 = s[:idx] + new
        open(p, 'w', encoding='utf-8', newline='').write(s2)
        print('patched skip-render, size', len(s2))
