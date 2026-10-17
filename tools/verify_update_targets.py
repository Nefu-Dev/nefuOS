#!/usr/bin/env python3
"""Host-side verification of the update.json targets:
   - llm_model.img from GitHub releases (302 redirect + Range)
   - nefuOS.iso from nefuos.site.je (InfinityFree JS challenge)
Both are streamed and SHA-256 compared against the manifest values.
"""
import hashlib, re, sys, time
from Crypto.Cipher import AES
import urllib.request

def to_n(h): return [int(h[i:i+2], 16) for i in range(0, len(h), 2)]
def to_h(b): return ''.join('%02x' % x for x in b)

def get_challenge_cookie(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    body = urllib.request.urlopen(req, timeout=40).read().decode('utf-8', 'replace')
    keys = dict(re.findall(r'(?:var\s+)?([abc])\s*=\s*toNumbers\("([0-9a-fA-F]+)"\)', body))
    if not keys:
        return None
    key = bytes(to_n(keys['a'])); iv = bytes(to_n(keys['b'])); ct = bytes(to_n(keys['c']))
    pt = AES.new(key, AES.MODE_CBC, iv).decrypt(ct)
    pad = pt[-1]
    if 1 <= pad <= 16 and pt[-pad:] == bytes([pad]) * pad:
        pt = pt[:-pad]
    return to_h(pt)

def stream(url, cookie=None, offset=0, range_test=False):
    hdrs = {'User-Agent': 'nefuOS/update-test/1.0'}
    if cookie:
        hdrs['Cookie'] = '__test=' + cookie
    if offset > 0:
        hdrs['Range'] = 'bytes=%d-' % offset
    req = urllib.request.Request(url, headers=hdrs)
    resp = urllib.request.urlopen(req, timeout=180)
    status = resp.status
    total = resp.headers.get('Content-Length')
    return resp, status, total

def sha256_of(resp, want_status):
    h = hashlib.sha256(); got = 0
    while True:
        b = resp.read(1 << 16)
        if not b: break
        h.update(b); got += len(b)
    return h.hexdigest(), got

print("=== 1) llm_model.img (GitHub, expect 302 redirect + Range) ===")
gurl = 'https://github.com/Nefu-Dev/nefuOS/releases/download/v3.0/llm_model.img'
resp, status, total = stream(gurl, offset=0)
print("status=%s content-length=%s" % (status, total))
digest, got = sha256_of(resp, status)
print("bytes=%d sha256=%s" % (got, digest))
expect = 'fb87a93a4cf8c61b3ae763b62f4b971b246ac16478c01150842a505c6e4fdd0c'
print("expect=%s  MATCH=%s" % (expect, digest == expect))

print()
print("=== 1b) llm_model Range test (resume support) ===")
resp, status, total = stream(gurl, offset=1000, range_test=True)
print("Range status=%s content-length=%s (expect 206)" % (status, total))

print()
print("=== 2) nefuOS.iso (nefuos.site.je, JS challenge) ===")
iurl = 'https://nefuos.site.je/nefuOS.iso'
cookie = get_challenge_cookie(iurl)
print("challenge cookie: %s" % (cookie if cookie else 'NONE (no challenge?)'))
if cookie:
    resp, status, total = stream(iurl, cookie=cookie)
    print("status=%s content-length=%s" % (status, total))
    digest, got = sha256_of(resp, status)
    print("bytes=%d sha256=%s" % (got, digest))
    iexp = '05d55a1b3f0dbcc5712fe1d3cad85b777634f14eee8aebba511e3cf65d708705'
    print("expect=%s  MATCH=%s" % (iexp, digest == iexp))
else:
    # maybe no challenge on this path
    resp, status, total = stream(iurl)
    print("status=%s content-length=%s" % (status, total))
    digest, got = sha256_of(resp, status)
    print("bytes=%d sha256=%s" % (got, digest))
print("DONE")
