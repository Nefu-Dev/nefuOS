import re, sys
p = sys.argv[1]
html = open(p, encoding='utf-8', errors='replace').read()
# collect <style> bodies
bodies = re.findall(r'<style[^>]*>(.*?)</style>', html, re.S)
text = '\n'.join(bodies)
print('style bytes', len(text))
# crude rule split
rules = re.findall(r'([^{}]+)\{([^{}]*)\}', text)
hides = [(s.strip(), b) for s, b in rules if re.search(r'display\s*:\s*none', b)]
print('rules', len(rules), 'hide rules', len(hides))
for s, b in hides[:60]:
    s = ' '.join(s.split())
    if len(s) > 90:
        s = s[:90] + '...'
    print('HIDE', s)
print('--- any rule mentioning #lg ---')
for s, b in rules:
    s2 = ' '.join(s.split())
    if s2.endswith('#lg') or s2 == '#lg' or '#lg' in s2.split(',')[-1]:
        print('LG', s2[:120], '=>', ' '.join(b.split())[:120])
