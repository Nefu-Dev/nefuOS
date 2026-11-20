import re, sys
p, needle = sys.argv[1], sys.argv[2]
back = int(sys.argv[3]) if len(sys.argv) > 3 else 300
html = open(p, encoding='utf-8', errors='replace').read()
bodies = [(m.start(), m.end()) for m in re.finditer(r'<style[^>]*>(.*?)</style>', html, re.S)]
for s, e in bodies:
    body = html[s:e]
    for m in re.finditer(re.escape(needle) + r'\s*\{', body):
        a = max(0, m.start() - back)
        print('=== at style block %d..%d offset %d' % (s, e, m.start()))
        print(body[a:m.start()].replace('\n', ' ')[-back:])
        print('----> DECL:', body[m.start():m.start()+120].replace('\n', ' '))
        print()
