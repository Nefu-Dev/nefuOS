# -*- coding: utf-8 -*-
import os
OUT = r"D:\mycppos1\nefuOS\verify_out"
os.makedirs(OUT, exist_ok=True)
line = b"/* t */ var x=1; // line\n"
data = bytearray()
while len(data) < 600:
    data += line
data = data[:600]
with open(OUT + r"\mini600.js", "wb") as f:
    f.write(bytes(data))
html = ('<html><head><title>t</title></head><body><script>'
        'var r=__nfuHttp("GET","http://127.0.0.1:8123/mini600.js","","");'
        'document.title="ok";</script></body></html>')
with open(OUT + r"\mini600.html", "w") as f:
    f.write(html)
print("created", os.path.getsize(OUT + r"\mini600.js"))
