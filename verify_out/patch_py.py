p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
s = open(p, encoding='utf-8', errors='replace').read()
old = 'function PY(jb){var bj=jb[tY];return void 0===bj&&(bj=jb[tY]=new Set),bj}'
new = 'function PY(jb){if(!jb||"object"!==typeof jb&&"function"!==typeof jb)return new Set;var bj=jb[tY];return void 0===bj&&(bj=jb[tY]=new Set),bj}'
n = s.count(old)
print('matches:', n)
if n:
    s2 = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s2)
    print('patched, new size', len(s2))
