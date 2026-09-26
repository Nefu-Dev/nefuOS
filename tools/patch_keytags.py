# -*- coding: utf-8 -*-
p = r'D:\mycppos1\nefuOS\backends\win32\win32.cpp'
s = open(p, encoding='utf-8').read()
old = '''                for (const char* p = s_key_str; *p; p++) {
                    char c = *p;
                    int kc = 0;
                    if (c == '\\r' || c == '\\n') kc = KEY_ENTER;
                    else if (c == 8) kc = KEY_BACKSPACE;
                    else if (c == 27) kc = KEY_ESC;

                    nefuos_handle_key(kc, c, true);'''
new = '''                for (const char* p = s_key_str; *p; p++) {
                    char c = *p;
                    int kc = 0;
                    if (c == '\\r' || c == '\\n') kc = KEY_ENTER;
                    else if (c == 8) kc = KEY_BACKSPACE;
                    else if (c == 27) kc = KEY_ESC;
                    else if (c == '<') {
                        // special-key tag like <PgDn> for scroll tests
                        const char* q = p + 1;
                        const char* e2 = strchr(q, '>');
                        if (e2 && e2 - q < 10) {
                            char tag[12]; int tl = (int)(e2 - q);
                            memcpy(tag, q, (size_t)tl); tag[tl] = 0;
                            int kc2 = 0;
                            if (!strcmp(tag, "PgDn")) kc2 = KEY_PGDN;
                            else if (!strcmp(tag, "PgUp")) kc2 = KEY_PGUP;
                            else if (!strcmp(tag, "Down")) kc2 = KEY_DOWN;
                            else if (!strcmp(tag, "Up")) kc2 = KEY_UP;
                            else if (!strcmp(tag, "Home")) kc2 = KEY_HOME;
                            else if (!strcmp(tag, "End")) kc2 = KEY_END;
                            if (kc2) {
                                nefuos_handle_key(kc2, 0, true);
                                nefuos_handle_key(kc2, 0, false);
                                p = e2;
                                Sleep(24);
                                continue;
                            }
                        }
                    }

                    nefuos_handle_key(kc, c, true);'''
print('count:', s.count(old))
assert s.count(old) == 1
s = s.replace(old, new, 1)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('key tags added')
