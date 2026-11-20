import base64, sys

def a0raw(h):
    v = base64.b64decode(h + "=" * (-len(h) % 4))
    u = 57865
    out = []
    p = 0
    while p < len(v):
        u = ((u << 8) | (u >> 8)) & 0xFFFF
        r = v[p] ^ (u >> 8); p += 1
        if r == 255:
            u = ((u << 8) | (u >> 8)) & 0xFFFF
            r = v[p] ^ (u >> 8); p += 1
            u = ((u << 8) | (u >> 8)) & 0xFFFF
            hb = v[p] ^ (u >> 8); p += 1
            r = r | (hb << 8)
        out.append(r & 0xFFFF)
    return out

for s in ["hidden", "UhlDDF0NcAFaBVc=", "QB1KBVY=", "EA1WXANcAQ==", "UQhQAlQbXBxdDXAGXwZB",
          "YYtthmyM", "epZwjmw=", "bYt6kmWDcA=="]:
    codes = a0raw(s)
    print(s, "->", codes, "ascii:", "".join(chr(c) if 32 <= c < 127 else "?" for c in codes))
