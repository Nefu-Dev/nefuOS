import urllib.request, gzip, io
req = urllib.request.Request('https://bloxd.io/6qxffk.main.js', headers={
    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
    'Accept-Encoding': 'gzip',
    'Referer': 'https://bloxd.io/?g=classic',
})
r = urllib.request.urlopen(req, timeout=30)
data = r.read()
if data[:2] == b'\x1f\x8b':
    data = gzip.decompress(data)
p = r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js'
open(p, 'wb').write(data)
print('refetched', len(data))
