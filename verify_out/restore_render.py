p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
s = open(p, encoding='utf-8', errors='replace').read()
old = 'setTimeout(function(){ try { (0,Ij.render)'
idx = s.find(old)
print('skip pat idx:', idx)
if idx >= 0:
    # find end: 'document.getElementById("root")) } catch(err){} }, 0)'
    e = s.find(' } catch(err){} }, 0)', idx)
    print('end:', e)
    if e >= 0:
        # extract inner render call
        inner_start = idx + len('setTimeout(function(){ try { ')
        inner = s[inner_start:e]
        print('inner tail:', repr(inner[-40:]))
        s2 = s[:idx] + inner + s[e+len(' } catch(err){} }, 0)'):]
        open(p, 'w', encoding='utf-8', newline='').write(s2)
        print('restored render, size', len(s2), 'tail:', repr(s2[-70:]))
