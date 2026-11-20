import sys, re
from html.parser import HTMLParser

skip = {'style', 'svg', 'path', 'br', 'noscript'}

class Node:
    __slots__ = ('tag', 'attrs', 'kids', 'text')
    def __init__(self, tag, attrs):
        self.tag = tag
        self.attrs = dict(attrs)
        self.kids = []
        self.text = ''

class Builder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node('#doc', [])
        self.stack = [self.root]
    def handle_starttag(self, tag, attrs):
        n = Node(tag, attrs)
        self.stack[-1].kids.append(n)
        if tag not in ('br', 'img', 'meta', 'link', 'input', 'hr', 'path'):
            self.stack.append(n)
    def handle_endtag(self, tag):
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                return
    def handle_data(self, data):
        t = data.strip()
        if t:
            self.stack[-1].text += t[:60]

def label(n):
    cls = n.attrs.get('class', '')
    cls = ' '.join(cls.split())[:46]
    idv = n.attrs.get('id', '')
    st = n.attrs.get('style', '')
    s = n.tag
    if idv:
        s += '#' + idv[:24]
    if cls:
        s += '.' + cls
    if st:
        s += ' [style:' + ' '.join(st.split())[:40] + ']'
    return s

def walk(n, d, maxd, out):
    if d > maxd:
        return
    if n.tag in skip:
        return
    out.append('  ' * d + label(n) + (('  <' + n.text + '>') if n.text else ''))
    for k in n.kids:
        walk(k, d + 1, maxd, out)

path = sys.argv[1]
maxd = int(sys.argv[2]) if len(sys.argv) > 2 else 8
needle = sys.argv[3] if len(sys.argv) > 3 else None
src = open(path, encoding='utf-8', errors='replace').read()
b = Builder()
b.feed(src)
out = []
for k in b.root.kids:
    walk(k, 0, maxd, out)
txt = '\n'.join(out)
if needle:
    lines = txt.split('\n')
    keep = [l for l in lines if needle in l]
    print('\n'.join(keep[:60]))
    print('--- matched %d lines ---' % len(keep))
else:
    print(txt)
