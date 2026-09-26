p = r'D:\mycppos1\nefuOS\core\apps\php2html.h'
with open(p, 'rb') as f:
    data = f.read()
old = (
    b'                } else {\n'
    b'                    FILE* dbg = fopen("php_fail.txt", "ab");\n'
    b'                    if (dbg) { fprintf(dbg, "FAIL tok=[%s] type=%d line=%d n=%d\\n", Lx.cur.text, Lx.cur.type, Lx.line, prog.nstmts); fclose(dbg); }\n'
    b'                }'
)
new = (
    b'                } else {\n'
    b'                    /* parse failed (logged via klogf below) */\n'
    b'                }'
)
n = data.count(old)
print('occurrences:', n)
if n != 1:
    raise SystemExit(1)
data = data.replace(old, new)
with open(p, 'wb') as f:
    f.write(data)
print('patched OK')
