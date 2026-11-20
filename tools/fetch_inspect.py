# -*- coding: utf-8 -*-
"""Fetch a URL and report where key markup sits relative to the 64 KB seed cut."""
import sys, urllib.request, gzip, io, re

url = sys.argv[1] if len(sys.argv) > 1 else "https://www.baidu.com/"
req = urllib.request.Request(url, headers={
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                  "(KHTML, like Gecko) Chrome/120.0 Safari/537.36",
    "Accept-Encoding": "gzip, deflate",
})
with urllib.request.urlopen(req, timeout=30) as r:
    data = r.read()
    enc = r.headers.get("Content-Encoding", "")
if enc == "gzip":
    data = gzip.decompress(data)
text = data.decode("utf-8", "replace")
print("bytes=%d chars=%d" % (len(data), len(text)))
for m in ("百度一下", 'id="kw"', 'id="su"', 'id="form"', 'id="main-wrapper"', "<script", "<style"):
    print("%-22s count=%d first_at=%d" % (m, text.count(m), text.find(m)))
# where is the body content that a reader sees
print("head:", text[:120].replace("\n", " "))
open(sys.argv[2] if len(sys.argv) > 2 else "tools/_fetch.html", "w", encoding="utf-8").write(text)
