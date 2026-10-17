#!/usr/bin/env python3
"""Prepare real file prefixes for resume tests."""
import re, urllib.request
from Crypto.Cipher import AES

def to_n(h): return [int(h[i:i+2],16) for i in range(0,len(h),2)]
def to_h(b): return ''.join('%02x'%x for x in b)
def cookie_of(url):
    req=urllib.request.Request(url, headers={'User-Agent':'Mozilla/5.0'})
    body=urllib.request.urlopen(req,timeout=40).read().decode('utf-8','replace')
    keys=dict(re.findall(r'(?:var\s+)?([abc])\s*=\s*toNumbers\("([0-9a-fA-F]+)"\)',body))
    if not keys: return None
    key=bytes(to_n(keys['a']));iv=bytes(to_n(keys['b']));ct=bytes(to_n(keys['c']))
    pt=AES.new(key,AES.MODE_CBC,iv).decrypt(ct); pad=pt[-1]
    if 1<=pad<=16 and pt[-pad:]==bytes([pad])*pad: pt=pt[:-pad]
    return to_h(pt)

def get(url, n, outfile, cookie=None):
    hdrs={'User-Agent':'nefuOS-update-test'}
    if cookie: hdrs['Cookie']='__test='+cookie
    req=urllib.request.Request(url,headers=hdrs)
    r=urllib.request.urlopen(req,timeout=120)
    data=r.read(n)
    open(outfile,'wb').write(data)
    print("wrote %d bytes -> %s (status=%s)"%(len(data),outfile,r.status))

# iso prefix (nefuos.site.je, challenge + ?i=1); retry because InfinityFree rate-limits
iurl='https://nefuos.site.je/nefuOS.iso'
import time
for attempt in range(6):
    ck=cookie_of(iurl)
    get(iurl+'?i=1', 100000, 'C:\\Users\\huawei\\AppData\\Local\\Temp\\nefu_upd_test\\iso_prefix.bin', ck)
    if open('C:\\Users\\huawei\\AppData\\Local\\Temp\\nefu_upd_test\\iso_prefix.bin','rb').read(5).startswith(b'<html'):
        print("iso prefix is a challenge page, retrying...")
        time.sleep(2)
    else:
        print("iso prefix OK")
        break

# llm prefix (GitHub)
gurl='https://github.com/Nefu-Dev/nefuOS/releases/download/v3.0/llm_model.img'
get(gurl, 50000, 'C:\\Users\\huawei\\AppData\\Local\\Temp\\nefu_upd_test\\llm_prefix.bin')

# also a WRONG prefix (junk) to test hash-mismatch deletion
open('C:\\Users\\huawei\\AppData\\Local\\Temp\\nefu_upd_test\\bad_prefix.bin','wb').write(b'X'*12345)
print("wrote bad_prefix.bin (12345 junk bytes)")
