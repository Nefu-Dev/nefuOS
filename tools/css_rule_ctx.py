import re, sys
p = sys.argv[1]
targets = sys.argv[2:] or ['#lg', '.s-isindex-wrap', '#u', '#s_tab']
html = open(p, encoding='utf-8', errors='replace').read()
bodies = re.findall(r'<style[^>]*>(.*?)</style>', html, re.S)
text = '\n'.join(bodies)
depth_stack = []
i = 0
n = len(text)
while i < n:
    c = text[i]
    if c in ' \t\r\n':
        i += 1; continue
    if text.startswith('/*', i):
        j = text.find('*/', i + 2)
        i = n if j < 0 else j + 2
        continue
    if c == '}':
        if depth_stack: depth_stack.pop()
        i += 1; continue
    st = i
    while i < n and text[i] not in '{};':
        i += 1
    prelude = ' '.join(text[st:i].split())
    if i >= n: break
    if text[i] == ';':
        i += 1; continue
    i += 1
    d = 1
    bs = i
    while i < n and d > 0:
        if text[i] == '{': d += 1
        elif text[i] == '}': d -= 1
        i += 1
    body = text[bs:i-1]
    if prelude.startswith('@'):
        depth_stack.append(prelude[:80])
        continue
    for t in targets:
        if t in prelude:
            ctx = ' <. '.join(depth_stack[-3:]) or '(top)'
            print('SEL', prelude[:110])
            print('   ctx:', ctx)
            print('   body:', ' '.join(body.split())[:120])
