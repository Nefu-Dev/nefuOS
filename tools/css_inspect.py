# -*- coding: utf-8 -*-
"""Report where a page's CSS actually lives: <link rel=stylesheet>, <style> blocks, textareas."""
import sys, re

p = sys.argv[1]
text = open(p, encoding="utf-8", errors="replace").read()
links = re.findall(r'<link[^>]+>', text, re.I)
print("links=%d style_links=%d" % (len(links), sum(1 for l in links if 'stylesheet' in l.lower())))
for l in links[:12]:
    if 'stylesheet' in l.lower():
        print("  LINK", l[:160])
styles = [(m.start(), m.end()) for m in re.finditer(r'<style[^>]*>', text, re.I)]
print("style blocks=%d" % len(styles))
for s, e in styles[:12]:
    end = text.find('</style>', e)
    print("  style at %d len=%d first=%.60s" % (s, end - e if end > 0 else -1, text[e:e + 60].replace("\n", " ")))
tas = [m.start() for m in re.finditer(r'<textarea', text, re.I)]
print("textareas=%d offsets=%s" % (len(tas), tas[:6]))
print("head range: %d..%d" % (text.find('<head'), text.find('</head>')))
