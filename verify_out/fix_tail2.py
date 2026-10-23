p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
s = open(p, encoding='utf-8', errors='replace').read()
bad = 'try { setTimeout(function(){ '
idx = s.rfind(bad)
print('bad idx:', idx, 'tail sample:', repr(s[idx:idx+220]))
if idx >= 0:
    # remove wrapper prefix
    s2 = s[:idx] + s[idx+len(bad):]
    # remove trailing wrapper: ' }, 0); } catch(e){};' and ensure '})();' after render
    tail_pat = ' }, 0); } catch(e){};'
    ti = s2.rfind(tail_pat)
    print('tail pat idx:', ti, 'before:', repr(s2[ti-30:ti]))
    if ti >= 0:
        s3 = s2[:ti] + s2[ti+len(tail_pat):]
        # render call now ends with "})()" -> make it "})();"
        rp = s3.rfind('})()')
        if rp >= 0 and rp > ti-400:
            s3 = s3[:rp] + '})();' + s3[rp+4:]
        open(p, 'w', encoding='utf-8', newline='').write(s3)
        print('restored, size', len(s3), 'tail:', repr(s3[-120:]))
