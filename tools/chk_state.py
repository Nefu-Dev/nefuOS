# -*- coding: utf-8 -*-
p = r'D:\mycppos1\nefuOS\core\apps\php2html.h'
s = open(p, encoding='utf-8').read()
print('postfix anchor:', s.count('u->op = lex_isop(L, "++") ? 2 : 3;'))
print('pe_un pdup:', s.count('const char* np = pdup(V, S->b);'))
print('php_blocks:', s.count('php_blocks'))
print('php_set_log:', s.count('php_set_log'))
i = s.find('postfix')
print('---around postfix---')
print(s[max(0,i-400):i+100] if i>0 else 'none')
