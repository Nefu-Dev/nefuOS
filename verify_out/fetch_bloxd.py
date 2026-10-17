import urllib.request, re, ssl, sys

ctx = ssl.create_default_context()
req = urllib.request.Request('https://bloxd.io/', headers={
    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/122.0 Safari/537.36',
    'Accept': 'text/html,application/xhtml+xml,*/*;q=0.8'})
try:
    html = urllib.request.urlopen(req, timeout=20, context=ctx).read().decode('utf-8', 'ignore')
    print('len', len(html))
    open('bloxd_local.html', 'w', encoding='utf-8').write(html)
    srcs = re.findall(r'<script[^>]*src=["\']([^"\']+)["\']', html)
    for s in srcs:
        print('SRC:', s)
    inl = re.findall(r'<script(?![^>]*src)[^>]*>(.*?)</script>', html, re.S)
    print('inline blocks:', len(inl), 'maxlen', max((len(x) for x in inl), default=0))
except Exception as e:
    print('ERR', repr(e))
