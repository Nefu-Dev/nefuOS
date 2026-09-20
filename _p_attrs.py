# -*- coding: utf-8 -*-
import io
path = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
s = io.open(path, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
old = ('                else if (!strcmp(tname,"img")) {\n'
       '                    // extract src attribute, resolve relative URLs against base\n'
       '                    String src = get_attr(html + tag_start, tag_text_len, "src");\n'
       '                    if (!src.empty() && strncmp(src.c_str(), "data:", 5) != 0) {')
new = ('                else if (!strcmp(tname,"img")) {\n'
       '                    // extract src (fall back to data-src / murl for lazy-loaded\n'
       '                    // images), resolve relative URLs against base\n'
       '                    String src = get_attr(html + tag_start, tag_text_len, "src");\n'
       '                    if (src.empty()) src = get_attr(html + tag_start, tag_text_len, "data-src");\n'
       '                    if (src.empty()) src = get_attr(html + tag_start, tag_text_len, "murl");\n'
       '                    if (!src.empty() && strncmp(src.c_str(), "data:", 5) != 0) {')
assert s.count(old) == 1
s = s.replace(old, new, 1)
io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('img attrs extended')
