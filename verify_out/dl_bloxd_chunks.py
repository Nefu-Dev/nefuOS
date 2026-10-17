import re, os, urllib.request

# Extract webpack jsonp chunk id->hash mapping from bloxd main bundle, then
# download every chunk into verify_out so game-page loads hit local cache.
main = r'D:\mycppos1\nefuOS\verify_out\y8i0fs.main.8a3f4681.js'
outdir = r'D:\mycppos1\nefuOS\verify_out'
data = open(main, 'rb').read().decode('utf-8', 'ignore')

# webpack jsonp chunks: {27:"991c24c2",28:"a5469f95",...}
pairs = re.findall(r'\{(\d+:"[0-9a-f]{8}"(?:,\d+:"[0-9a-f]{8}")*)\}', data)
chunks = {}
for p in pairs:
    for m in re.finditer(r'(\d+):"([0-9a-f]{8})"', p):
        chunks[int(m.group(1))] = m.group(2)
print('chunk ids found:', len(chunks))

ua = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/126.0.0.0'}
ok = fail = 0
for cid in sorted(chunks):
    name = f'y8i0fs.{cid}.{chunks[cid]}.chunk.js'
    path = os.path.join(outdir, name)
    if os.path.exists(path):
        ok += 1
        continue
    try:
        req = urllib.request.Request(f'https://bloxd.io/static/js/{name}', headers=ua)
        body = urllib.request.urlopen(req, timeout=20).read()
        open(path, 'wb').write(body)
        ok += 1
    except Exception as e:
        fail += 1
        print('FAIL', name, e)
print(f'downloaded/ok={ok} fail={fail}')
