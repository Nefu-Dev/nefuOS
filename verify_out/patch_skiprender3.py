p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
s = open(p, encoding='utf-8', errors='replace').read()
idx = s.rfind('(0,Ij.render)')
print('idx:', idx)
if idx >= 0:
    e = s.find('document.getElementById("root"))', idx)
    print('end idx:', e, 'tail after:', repr(s[e+30:e+45]))
    if e >= 0:
        call = s[idx:e+len('document.getElementById("root"))')]
        print('call tail:', repr(call[-40:]))
        new = 'setTimeout(function(){ try { ' + call + ' } catch(err){} }, 0)'
        s2 = s[:idx] + new + s[e+len('document.getElementById("root"))'):]
        open(p, 'w', encoding='utf-8', newline='').write(s2)
        print('patched skip-render, size', len(s2), 'tail:', repr(s2[-90:]))
