def dbg(base, href):
    out = ""
    if not href or not base: return
    if href.startswith("http://") or href.startswith("https://") or href.startswith("file://") or href.startswith("data:"):
        print("abs passthrough", href); return
    b = base.split("?",1)[0].split("#",1)[0]
    m = b.find("://")
    if m < 0:
        print("no scheme -> empty"); return
    hostend = m + 3
    while hostend < len(b) and b[hostend] != '/': hostend += 1
    out = b[:hostend]
    print("after scheme copy:", repr(out), "hostend", hostend, "b[hostend:]", repr(b[hostend:]))
    if href.startswith("//"):
        print("protocol-relative", out + href); return
    if not href.startswith("/"):
        out += b[hostend:]
        last = -1
        for i in range(len(out)-1, 0, -1):
            if out[i] == '/': last = i; break
        if last < 0:
            out += '/'
        else:
            out = out[:last+1]
        print("after dir:", repr(out))
    out += href
    print("joined:", repr(out))
    tmp = out
    sch = tmp.find("://")
    root = 0
    if sch >= 0:
        root = sch + 3
        while root < len(tmp) and tmp[root] != '/': root += 1
        if root < len(tmp): root += 1
    print("root", root, "tmp[8]", repr(tmp[8]) if len(tmp)>8 else "EOF")
    res = tmp[:root-1] if root > 0 else ""
    print("res head:", repr(res), "len", len(res))
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
    print("segs:", segs)
    for s in segs:
        res += '/' + s
    if not segs: res += '/'
    print("FINAL:", repr(res))

dbg("https://bloxd.io", "../../static/js/y8i0fs.27.chunk.js")
