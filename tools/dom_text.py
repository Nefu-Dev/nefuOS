import io, re, sys

p = sys.argv[1]
text = io.open(p, "r", encoding="utf-8", errors="replace").read()
# strip style blocks so the visible text stands out
text = re.sub(r"<style[^>]*>.*?</style>", " ", text, flags=re.S)
# every tag becomes a separator
text = re.sub(r"<[^>]+>", "\n", text)
lines = [l.strip() for l in text.split("\n")]
keep = [l for l in lines if l]
seen = set()
out = []
for l in keep:
    if len(l) > 120:
        l = l[:120]
    if l in seen:
        continue
    seen.add(l)
    out.append(l)
print("nodes_text=%d unique=%d" % (len(keep), len(out)))
for l in out[:140]:
    print(l)
