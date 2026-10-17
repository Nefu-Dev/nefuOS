#!/usr/bin/env python3
"""Solve the InfinityFree/ProFreeHost JS __test anti-bot challenge.

Flow (mirrors what the browser does):
  1. GET the URL -> server returns an HTML+JS page embedding slowAES params a,b,c.
  2. Compute __test = toHex(slowAES.decrypt(c, 2, a, b)).
  3. Re-request the URL with Cookie: __test=<value> (and usually ?i=1), get the real body.
"""
import sys, re, time, hashlib, argparse
from Crypto.Cipher import AES
try:
    import requests
except Exception:
    requests = None


def to_numbers(hx: str):
    return [int(hx[i:i + 2], 16) for i in range(0, len(hx), 2)]


def to_hex(bs):
    return "".join("%02x" % b for b in bs)


def slowaes_decrypt(cipher_hex, level, key_hex, iv_hex):
    """slowAES.decrypt(c, level, key, cbc_iv). level ignored for length; we
    pick AES-128/192/256 by the key length like slowAES's key_size table would
    (key here is always 32 hex = AES-128)."""
    key = bytes(to_numbers(key_hex))
    iv = bytes(to_numbers(iv_hex))
    ct = bytes(to_numbers(cipher_hex))
    alg = {16: "AES128", 24: "AES192", 32: "AES256"}.get(len(key), "AES128")
    if alg == "AES128":
        cipher = AES.new(key, AES.MODE_CBC, iv)
    elif alg == "AES192":
        from Crypto.Cipher import AES as A
        cipher = A.new(key, A.MODE_CBC, iv)
    else:
        from Crypto.Cipher import AES as A
        cipher = A.new(key, A.MODE_CBC, iv)
    pt = cipher.decrypt(ct)
    # strip PKCS7 padding
    pad = pt[-1]
    if 1 <= pad <= 16 and pt[-pad:] == bytes([pad]) * pad:
        pt = pt[:-pad]
    return pt


def fetch_raw(url, cookie=None, extra_path=None, verify=True):
    hdrs = {"User-Agent": "Mozilla/5.0"}
    if cookie:
        hdrs["Cookie"] = "__test=%s" % cookie
    if requests:
        r = requests.get(url, headers=hdrs, timeout=30, verify=verify)
        return r.status_code, r.text, r.headers
    # urllib fallback
    import urllib.request
    req = urllib.request.Request(url, headers=hdrs)
    with urllib.request.urlopen(req, timeout=30) as resp:
        body = resp.read().decode("utf-8", "replace")
        return resp.status, body, resp.headers


def solve(url, verify=True):
    status, body, hdrs = fetch_raw(url, verify=verify)
    m = re.search(r'slowAES\.decrypt\(toNumbers\("([0-9a-fA-F]+)"\),\s*(\d+),\s*toNumbers\("([0-9a-fA-F]+)"\)\s*,\s*toNumbers\("([0-9a-fA-F]+)"\)\)', body)
    if not m:
        m2 = re.search(r'slowAES\.decrypt\(c,\s*(\d+),\s*a,\s*b\)', body)
        if not m2:
            # no challenge
            return status, body, ""
        # keys are defined by assignment lines: var a=toNumbers("..."),b=toNumbers("..."),c=toNumbers("...")
        keys = dict(re.findall(r'(?:var\s+)?([abc])\s*=\s*toNumbers\("([0-9a-fA-F]+)"\)', body))
        level = int(m2.group(1))
        cookie = to_hex(slowaes_decrypt(keys["c"], level, keys["a"], keys["b"]))
        return status, body, cookie
    cipher = m.group(1)
    level = int(m.group(2))
    key = m.group(3)
    iv = m.group(4)
    cookie = to_hex(slowaes_decrypt(cipher, level, key, iv))
    return status, body, cookie


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("--insecure", action="store_true", help="disable TLS verification")
    args = ap.parse_args()
    verify = not args.insecure
    status, body, cookie = solve(args.url, verify=verify)
    if not cookie:
        print("NO CHALLENGE / plain response:\n" + body)
        return
    # Re-request with the cookie (append ?i=1 like the page does)
    sep = "&" if "?" in args.url else "?"
    url2 = args.url + sep + "i=1"
    s2, body2, hdrs = fetch_raw(url2, cookie=cookie, verify=verify)
    print("challenge cookie: %s" % cookie)
    print("re-request status: %s" % s2)
    print("----- BODY -----")
    print(body2)


if __name__ == "__main__":
    main()
