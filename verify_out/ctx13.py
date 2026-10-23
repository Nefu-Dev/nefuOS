s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
start = s.index('(()=>{const jb=atob("RXJyb3I=")')
end = s.index('),,(jb,bj,Oj)=>{', start) + 2  # include '})(' hmm just up to the IIFE close
# find the closing '})()' right before '},,'
seg = s[start:start+800]
print('seg len', len(seg))
print(repr(seg))
# locate IIFE end: '})()' followed by '},,'
i_ii = seg.index('})()')
print('IIFE end at', i_ii)
closure = seg[:i_ii+4]
print('closure len', len(closure))
print(repr(closure[-120:]))
print('has ")?', '")' in closure)
# check for any ')' issues with raw string: R"(...)" terminates at ")"
print('rawstring-safe:', '")' not in closure)
