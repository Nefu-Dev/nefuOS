# -*- coding: utf-8 -*-
decls = {
    r'D:\mycppos1\nefuOS\core\datlib\vec.h': ('\n} // namespace dt\n} // namespace nefu\n', '\nint vec_self_test();\n\n} // namespace dt\n} // namespace nefu\n'),
    r'D:\mycppos1\nefuOS\core\datlib\ringbuf.h': ('\n} // namespace dt\n} // namespace nefu\n', '\nint ringbuf_self_test();\n\n} // namespace dt\n} // namespace nefu\n'),
    r'D:\mycppos1\nefuOS\core\datlib\bitset.h': ('\n} // namespace dt\n} // namespace nefu\n', '\nint bitset_self_test();\n\n} // namespace dt\n} // namespace nefu\n'),
    r'D:\mycppos1\nefuOS\core\datlib\hashtab.h': ('\n} // namespace dt\n} // namespace nefu\n', '\nint hashtab_self_test();\n\n} // namespace dt\n} // namespace nefu\n'),
}
for p, (old, new) in decls.items():
    raw = open(p, 'rb').read()
    s = raw.decode('utf-8-sig')
    if s.count(old) != 1:
        raise SystemExit('NOT UNIQUE in %s: %d' % (p, s.count(old)))
    s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='').write(s)
    print('OK', p)
