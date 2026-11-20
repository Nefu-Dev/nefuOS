import re, sys
p = sys.argv[1]
html = open(p, encoding='utf-8', errors='replace').read()
bodies = re.findall(r'<style[^>]*>(.*?)</style>', html, re.S)
text = '\n'.join(bodies)

# walk the sheet, tracking the @media/@supports prelude we are inside
depth = 0
stack = []
i = 0
n = len(text)
out = []
rules = 0
while i < n:
    c = text[i]
    if c in ' \t\r\n':
        i += 1; continue
    if text.startswith('/*', i):
        j = text.find('*/', i + 2)
        i = n if j < 0 else j + 2
        continue
    if c == '}':
        if stack: stack.pop()
        i += 1; continue
    # read prelude up to '{' or ';'
    st = i
    while i < n and text[i] not in '{};':
        i += 1
    prelude = ' '.join(text[st:i].split())
    if i >= n: break
    if text[i] == ';':
        i += 1; continue
    i += 1  # '{'
    # read body to matching brace (shallow)
    d = 1
    bs = i
    while i < n and d > 0:
        if text[i] == '{': d += 1
        elif text[i] == '}': d -= 1
        i += 1
    body = text[bs:i-1]
    if prelude.startswith('@'):
        stack.append(prelude[:60])
        continue
    rules += 1
    if re.search(r'display\s*:\s*none', body):
        ctx = ' | '.join(stack[-2:])
        out.append((prelude[:100], ctx))

print('rules walked', rules, 'hides', len(out))
targets = ('#lg', '#u', 's-isindex-wrap', 'result_logo', '#s_tab', 'wrapper_wrapper')
for sel, ctx in out:
    if any(t in sel for t in targets):
        print('HIDE', sel, '|| ctx:', ctx if ctx else '(none)')
