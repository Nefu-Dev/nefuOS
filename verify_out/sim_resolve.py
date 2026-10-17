import re

def js_url_resolve(base, href):
    out = ""
    if not href or not base: return out
    if href.startswith("http://") or href.startswith("https://") or href.startswith("file://") or href.startswith("data:"):
        return href
    b = base.split("?",1)[0].split("#",1)[0]
    m = b.find("://")
    if m < 0: return ""
    hostend = m + 3
    while hostend < len(b) and b[hostend] != '/': hostend += 1
    out = b[:hostend]
    if href.startswith("//"):
        return out + href
    if not href.startswith("/"):
        pp = hostend
        out += b[hostend:]
        last = -1
        for i in range(len(out)-1, 0, -1):
            if out[i] == '/': last = i; break
        if last < 0: out += '/'
        else: out = out[:last+1]
    out += href
    # normalization stack (mirror of C code)
    tmp = out
    sch = tmp.find("://")
    root = 0
    if sch >= 0:
        root = sch + 3
        while root < len(tmp) and tmp[root] != '/': root += 1
        if root < len(tmp): root += 1
    res = tmp[:root-1] if root > 0 else ""
    i = root
    if i < len(tmp) and tmp[i] == '/': i += 1
    segs = []
    while i < len(tmp):
        s = ""
        while i < len(tmp) and tmp[i] != '/':
            s += tmp[i]; i += 1
        if s == "":
            if i < len(tmp): i += 1
            continue
        if s == ".":
            if i < len(tmp): i += 1
            continue
        if s == "..":
            if segs: segs.pop()
            if i < len(tmp): i += 1
            continue
        segs.append(s)
        if i < len(tmp): i += 1
    for s in segs:
        res += '/' + s
    if not segs: res += '/'
    return res

for base, href in [
    ("https://bloxd.io", "../../static/js/y8i0fs.27.991c24c2.chunk.js"),
    ("https://bloxd.io/", "../../static/js/y8i0fs.27.991c24c2.chunk.js"),
    ("https://bloxd.io/static/js/y8i0fs.main.8a3f4681.js", "../../static/js/y8i0fs.27.chunk.js"),
]:
    print(base, "|", href, "->", js_url_resolve(base, href))
