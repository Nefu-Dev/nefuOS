#!/usr/bin/env python3
import hashlib, re, urllib.request
from Crypto.Cipher import AES

def to_n(h): return [int(h[i:i+2], 16) for i in range(0, len(h), 2)]
def to_h(b): return ''.join('%02x' % x for x in b)

def challenge(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    body = urllib.request.urlopen(req, timeout=40).read().decode('utf-8', 'replace')
    keys = dict(re.findall(r'(?:var\s+)?([abc])\s*=\s*toNumbers\("([0-9a-fA-F]+)"\)', body))
    if not keys: return None
    key = bytes(to_n(keys['a'])); iv = bytes(to_n(keys['b'])); ct = bytes(to_n(keys['c']))
    pt = AES.new(key, AES.MODE_CBC, iv).decrypt(ct)
    pad = pt[-1]
    if 1 <= pad <= 16 and pt[-pad:] == bytes([pad]) * pad: pt = pt[:-pad]
    return to_h(pt)

iurl = 'https://nefuos.site.je/nefuOS.iso'
cookie = challenge(iurl)
print("cookie:", cookie)

for label, u in [("no-?i", iurl), ("?i=1", iurl + "?i=1"), ("?i=1 twice", iurl + "?i=1")]:
    req = urllib.request.Request(u, headers={'User-Agent': 'Mozilla/5.0',
                                             'Cookie': '__test=' + cookie,
                                             'Range': 'bytes=0-65535'})
    try:
        resp = urllib.request.urlopen(req, timeout=60)
        data = resp.read()
        print("%s: status=%s len=%d head=%r" % (label, resp.status, len(data), data[:40]))
        if len(data) > 1000 and not data[:5].startswith(b'<html'):
            h = hashlib.sha256(); got = 0
            # full download
            req2 = urllib.request.Request(u, headers={'User-Agent': 'Mozilla/5.0',
                                                      'Cookie': '__test=' + cookie})
            r2 = urllib.request.urlopen(req2, timeout=120)
            while True:
                b = r2.read(1 << 16)
                if not b: break
                h.update(b); got += len(b)
            print("full: bytes=%d sha256=%s" % (got, h.hexdigest()))
    except Exception as e:
        print("%s: ERROR %s" % (label, e))
