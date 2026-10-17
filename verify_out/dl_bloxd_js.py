import urllib.request, ssl, os, re

ctx = ssl.create_default_context()
UA = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/122.0 Safari/537.36'}

urls = [
    'https://bloxd.io/static/js/y8i0fs.main.8a3f4681.js',
    'https://cdn.jsdelivr.net/npm/vconsole@3.15.1/dist/vconsole.min.js',
    'https://www.recaptcha.net/recaptcha/api.js?onload=onRecaptchaLoaded&render=6LcPavIpAAAAANyg3LnQvmaWqESD8DUTRgQ7TRDk',
    'https://static.cloudflareinsights.com/beacon.min.js/v31edd6df95cf4e85bb4c19e7a9bdbcba1788362987495',
]

def fetch(url, timeout=25):
    try:
        req = urllib.request.Request(url, headers=UA)
        data = urllib.request.urlopen(req, timeout=timeout, context=ctx).read()
        return data
    except Exception as e:
        return None

for u in urls:
    data = fetch(u)
    if data is None:
        print('FAIL', u[:70])
        continue
    base = os.path.basename(u.split('?')[0])
    if not base:
        base = 'index.js'
    with open(base, 'wb') as f:
        f.write(data)
    print('OK  %s  %d bytes' % (base, len(data)))
