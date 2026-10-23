p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
s = open(p, encoding='utf-8', errors='replace').read()
old = 'function sV(jb){jb[BV]||(jb[BV]=!0,Ij.forEach((function(bj){MV.has(bj)||yV(bj,!1,jb,null),yV(bj,!0,jb,null)})))}'
new = 'function sV(jb){if(!jb||"object"!==typeof jb&&"function"!==typeof jb)return;jb[BV]||(jb[BV]=!0,Ij.forEach((function(bj){MV.has(bj)||yV(bj,!1,jb,null),yV(bj,!0,jb,null)})))}'
print('sV matches:', s.count(old))
if s.count(old):
    s = s.replace(old, new)
old2 = 'function LV(jb,bj,Oj,Cj){var Vj=vO.get(bj);switch(void 0===Vj?2:Vj)'
new2 = 'function LV(jb,bj,Oj,Cj){if(!jb)return;var Vj=vO.get(bj);switch(void 0===Vj?2:Vj)'
print('LV matches:', s.count(old2))
if s.count(old2):
    s = s.replace(old2, new2)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('patched, size', len(s))
