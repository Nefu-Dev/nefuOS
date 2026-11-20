"""Score nefuOS renders against Edge references for the parity matrix.

For every <slug>.edge.png / <slug>.nefu.png pair in a directory:
  * downsample both to a common 320x256 grid (no aspect squash: letterbox),
  * report mean absolute RGB difference and a coarse 8x8 block histogram
    similarity, so a page that is structurally wrong cannot score high just
    because both are mostly white,
  * pull engine facts out of <slug>.nefu.out / <slug>.layout.txt.
Outputs parity_scores.csv and parity_report.html next to the images.
"""
import os, re, sys, csv, html

from PIL import Image

W, H = 320, 256


def load(path, crop=None):
    im = Image.open(path).convert('RGB')
    if crop:
        im = im.crop(crop)
    # keep aspect ratio, letterbox onto a white canvas
    im.thumbnail((W, H), Image.LANCZOS)
    canvas = Image.new('RGB', (W, H), (255, 255, 255))
    canvas.paste(im, ((W - im.width) // 2, 0))
    return canvas


def block_sig(im, n=8):
    """n x n grid of average colours: catches layout/background differences."""
    small = im.resize((n, n), Image.BOX)
    return list(small.getdata())


def sig_similarity(a, b):
    total = 0.0
    for (r1, g1, b1), (r2, g2, b2) in zip(a, b):
        total += 1.0 - (abs(r1 - r2) + abs(g1 - g2) + abs(b1 - b2)) / (3 * 255.0)
    return total / len(a)


def engine_facts(out_path, lay_path):
    facts = {'commit': '', 'runs': '', 'height': '', 'js_err': '', 'net': '', 'cause': ''}
    if os.path.exists(out_path):
        raw = open(out_path, 'rb').read().decode('utf-8', 'replace')
        m = re.search(r'commit \S+ nodes=(\d+) css=(\d+) js_err=(\d+)', raw)
        if m:
            facts['commit'] = 'nodes=%s css=%s' % (m.group(1), m.group(2))
            facts['js_err'] = m.group(3)
        if 'Cannot reach' in raw or 'load failed' in raw:
            facts['commit'] = facts['commit'] or 'LOAD FAILED'
        # a TLS/DNS failure means "no content to compare", which must not be
        # scored as a rendering difference
        m = re.search(r'connect fail err=\d+', raw)
        if m:
            facts['net'] = 'NET-FAIL ' + m.group(0).split('=')[-1]
        # Distinguish "our renderer got it wrong" from "the server refused to
        # serve a browser it considers a bot". A 403/Cloudflare challenge or a
        # cookie-driven 302 loop is the site's decision, not a layout defect.
        codes = re.findall(r'tls: status (\d{3})', raw)
        redirects = sum(1 for c in codes if c.startswith('3'))
        bad = [c for c in codes if c in ('401', '403', '405', '429', '503')]
        zero_css = len(re.findall(r'external css \d+ bytes -> 0 rules', raw))
        if facts['net']:
            facts['cause'] = 'DNS/TCP 不可达'
        elif redirects >= 3:
            facts['cause'] = '重定向循环（%d 次）' % redirects
        elif bad:
            facts['cause'] = '站点反爬拦截（HTTP %s）' % bad[-1]
        elif zero_css >= 2:
            facts['cause'] = '外链 CSS 仅含变量定义→0 规则'
        elif 'InternalError: interrupted' in raw:
            facts['cause'] = 'JS 预算中断（InternalError）'
        m = re.search(r'layout_page nodes=\d+ runs=(\d+) height=(\d+)', raw)
        if m:
            facts['runs'], facts['height'] = m.group(1), m.group(2)
    if os.path.exists(lay_path):
        head = open(lay_path, encoding='utf-8', errors='replace').readline()
        m = re.search(r'runs=(\d+) height=(\d+)', head)
        if m:
            facts['runs'] = m.group(1)
            facts['height'] = m.group(2)
    return facts


def ink_ratio(im):
    """Fraction of "content" pixels: anything that is not near-white.

    A page that renders mostly blank scores high on a mean-colour comparison,
    so this is the metric that exposes "expected a portal, got an empty sheet".
    """
    px = list(im.getdata())
    n = 0
    for r, g, b in px:
        if r < 240 or g < 240 or b < 240 or max(r, g, b) - min(r, g, b) > 16:
            n += 1
    return n / float(len(px))


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else 'verify_out/matrix'
    names = {}
    for f in sorted(os.listdir(d)):
        m = re.match(r'([a-z0-9_]+)\.(edge|nefu)\.png$', f)
        if m:
            names.setdefault(m.group(1), {})[m.group(2)] = os.path.join(d, f)

    rows = []
    for slug, pair in sorted(names.items()):
        if 'edge' not in pair or 'nefu' not in pair:
            rows.append({'site': slug, 'mad': '', 'sig': '', 'verdict': '重新补图',
                         'notes': '', 'commit': '', 'runs': '', 'height': '', 'js_err': ''})
            continue
        # nefuOS screenshots include the browser chrome: keep only the page
        # viewport box (title bar / toolbar / status bar excluded, 755x480 at
        # 20,150 in the 1000x800 framebuffer)
        a, b = load(pair['edge']), load(pair['nefu'], crop=(20, 150, 775, 630))
        pa, pb = list(a.getdata()), list(b.getdata())
        mad = sum(abs(x - y) for p1, p2 in zip(pa, pb) for x, y in zip(p1, p2)) / (len(pa) * 3)
        sig = sig_similarity(block_sig(a), block_sig(b))
        ia, ib = ink_ratio(a), ink_ratio(b)
        facts = engine_facts(pair['nefu'][:-3] + 'out', pair['nefu'][:-3] + 'layout.txt')
        if facts['net'] or facts['cause'].startswith(('站点反爬', '重定向循环')):
            verdict = 'X 站点侧拦截'
            notes = facts['cause']
        elif ia >= 0.02 and ib < ia * 0.35:
            verdict = 'D 内容缺失'
            notes = '内容像素 Edge %.1f%% / nefuOS %.1f%% %s' % (ia * 100, ib * 100, facts['cause'])
        elif sig >= 0.90 and mad <= 40:
            verdict = 'A 一致'
            notes = '内容像素 Edge %.1f%% / nefuOS %.1f%% %s' % (ia * 100, ib * 100, facts['cause'])
        elif sig >= 0.80:
            verdict = 'B 接近'
            notes = '内容像素 Edge %.1f%% / nefuOS %.1f%% %s' % (ia * 100, ib * 100, facts['cause'])
        elif sig >= 0.65:
            verdict = 'C 部分'
            notes = '内容像素 Edge %.1f%% / nefuOS %.1f%% %s' % (ia * 100, ib * 100, facts['cause'])
        else:
            verdict = 'D 结构不同'
            notes = '内容像素 Edge %.1f%% / nefuOS %.1f%% %s' % (ia * 100, ib * 100, facts['cause'])
        rows.append({'site': slug, 'mad': '%.1f' % mad, 'sig': '%.3f' % sig,
                     'verdict': verdict, 'notes': notes,
                     'commit': facts['commit'], 'runs': facts['runs'],
                     'height': facts['height'], 'js_err': facts['js_err']})

    csv_path = os.path.join(d, 'parity_scores.csv')
    with open(csv_path, 'w', newline='', encoding='utf-8') as fh:
        w = csv.DictWriter(fh, fieldnames=['site', 'mad', 'sig', 'verdict', 'commit',
                                           'runs', 'height', 'js_err', 'notes'])
        w.writeheader()
        w.writerows(rows)

    # html report with the actual pictures side by side
    parts = ['''<!DOCTYPE html><html lang="zh"><meta charset="utf-8">
<title>nefuOS 浏览器 vs Edge 渲染比对</title>
<style>
:root{color-scheme:light dark}
body{font:14px/1.6 -apple-system,"Segoe UI",system-ui,sans-serif;margin:0;padding:28px 32px;
     background:#0d1117;color:#e6edf3}
h1{font-size:20px;margin:0 0 6px}
h2{font-size:15px;font-weight:600;margin:28px 0 10px;color:#79c0ff}
p.lead{color:#8b949e;margin:0 0 18px}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}
th,td{padding:6px 10px;border-bottom:1px solid #21262d;text-align:left;font-size:13px}
th{color:#8b949e;font-weight:500;position:sticky;top:0;background:#0d1117}
.A{color:#3fb950}.B{color:#d29922}.C{color:#db6d28}.D{color:#f85149}
.sig{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:8px 0 26px}
.shot{border:1px solid #30363d;border-radius:8px;overflow:hidden;background:#161b22}
.shot img{display:block;width:100%}
.shot .cap{padding:5px 8px;font-size:12px;color:#8b949e}
</style><body>
<h1>nefuOS 自研浏览器 / Edge 渲染比对</h1>
<p class="lead">同一视口（746×560 页面区）截图。sig = 8×8 区块平均色相似度（抓版式与底色差异），
mad = 逐像素平均绝对差。右侧为 nefuOS 自研引擎（QuickJS + 自研 CSS/布局/绘制）。</p>
<table><tr><th>站点</th><th>sig</th><th>mad</th><th>判定</th><th>内容像素 Edge/nefuOS</th><th>commit</th><th>runs</th><th>页高</th><th>js_err</th></tr>''']
    for r in rows:
        cls = r['verdict'][:1]
        parts.append('<tr><td>%s</td><td class="%s">%s</td><td>%s</td><td class="%s">%s</td>'
                     '<td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>' % (
                         html.escape(r['site']), cls, r['sig'], r['mad'], cls,
                         html.escape(r['verdict']), html.escape(r['notes']),
                         html.escape(r['commit']), r['runs'], r['height'], r['js_err']))
    parts.append('</table>')
    for r in rows:
        slug = r['site']
        e = os.path.join(d, slug + '.edge.png')
        n = os.path.join(d, slug + '.nefu.png')
        if not (os.path.exists(e) and os.path.exists(n)):
            continue
        parts.append('<h2>%s &nbsp;<span class="%s">%s</span></h2><div class="sig">' % (
            html.escape(slug), r['verdict'][:1], html.escape(r['verdict'])))
        parts.append('<div class="shot"><img src="%s.edge.png"><div class="cap">Edge</div></div>' % slug)
        parts.append('<div class="shot"><img src="%s.nefu.png"><div class="cap">nefuOS</div></div>' % slug)
        parts.append('</div>')
    parts.append('</body></html>')
    html_path = os.path.join(d, 'parity_report.html')
    with open(html_path, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(parts))
    print(csv_path)
    print(html_path)
    for r in rows:
        print('%-14s sig=%s mad=%s %s' % (r['site'], r['sig'], r['mad'], r['verdict']))


if __name__ == '__main__':
    main()
