p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
s = open(p, encoding='utf-8', errors='replace').read()
idx = s.rfind('try { setTimeout(function(){ (0,Ij.render)')
print('bad patch idx:', idx)
if idx >= 0:
    print('tail now:', repr(s[idx:idx+160]))
    # rebuild: original was (0,Ij.render)(...})(); 
    inner_start = idx + len('try { setTimeout(function(){ ')
    # find IIFE end "})();" after render call
    e = s.find('})();', inner_start)
    print('iife end:', e)
    if e >= 0:
        orig = s[inner_start:e+4] + ';\n'
        s2 = s[:idx] + orig + s[e+4:]
        open(p, 'w', encoding='utf-8', newline='').write(s2)
        print('restored, size', len(s2), 'tail:', repr(s2[-80:]))
