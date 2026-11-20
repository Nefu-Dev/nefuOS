import base64, string
samples = ["eYd7hGaQZINngWw=", "bod9p2eWe4tskQ==", "Z41nhw==", "YYtthmyM",
           "f4t6i2uLZYt9mw==", "epZwjmw=", "bYt6kmWDcA==", "eJdskHCxbI5sgX2New==",
           "VoFvvWqKZb1mkn0=", "J48ljT+C", "LF0p", "Klw5fCNNP1AoSg==", "IVwjXjlR",
           "O584gg==", "FIkttSiCJ7Ukmj8=", "Op8umDK5LoYuiT+FOQ=="]
ok = set(string.ascii_letters + string.digits + "_/-.#$")
for s in samples:
    b = base64.b64decode(s + "=" * (-len(s) % 4))
    hits = []
    for k in range(256):
        x = bytes(c ^ k for c in b)
        # printable ascii heuristic
        if all(c in ok for c in x) and any(chr(c).isalpha() for c in x):
            hits.append((k, x.decode()))
    print(s, "->", hits[:6])
