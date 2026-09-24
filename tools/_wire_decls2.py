# -*- coding: utf-8 -*-
for p, name in [
    (r'D:\mycppos1\nefuOS\core\datlib\treap.h', 'treap_self_test'),
    (r'D:\mycppos1\nefuOS\core\datlib\btree.h', 'btree_self_test'),
]:
    raw = open(p, 'rb').read()
    s = raw.decode('utf-8-sig')
    old = '} // namespace dt\n} // namespace nefu\n'
    assert s.count(old) == 1, p
    s = s.replace(old, '\nint %s();\n\n%s' % (name, old))
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('OK', p)
