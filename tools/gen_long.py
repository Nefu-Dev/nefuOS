# -*- coding: utf-8 -*-
cards = []
for i in range(300):
    bg = '#B45309' if i % 2 == 0 else '#1D4ED8'
    cards.append('<div class="card" style="background:%s;padding:8px;margin:4px;border:2px solid #22C55E">Item %d - row content text</div>' % (bg, i))
html = ('<html><head><style>body{margin:0;background:#111} .card{color:#fff;font-size:14px}</style></head><body>'
        + '\n'.join(cards) + '</body></html>')
with open(r'D:\mycppos1\nefu_fix_web\long_test.html', 'w', encoding='utf-8') as f:
    f.write(html)
print('long_test.html written', len(html))
