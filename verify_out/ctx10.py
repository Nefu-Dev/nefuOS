s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
lines = s.split('\n')
base = 0
for i in range(1):
    base += len(lines[i]) + 1
# grab from the IIFE start through Xj definition + call
start = s.find('(()=>{const jb=atob("RXJyb3I="),', base)
if start < 0:
    start = s.find('(()=>{const jb=atob', base)
print('start', start)
# find end: the Xj body followed by })()
end = s.find('}return!1}})()}', start)
if end < 0:
    end = start + 8000
print('len', end - start)
open(r'D:\mycppos1\nefuOS\verify_out\xj_closure.js', 'w', encoding='utf-8').write(s[start:end+14])
print('written')
