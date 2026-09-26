import re, io

def patch(path, subs):
    with open(path, 'rb') as f:
        data = f.read()
    for old, new, must in subs:
        n = data.count(old)
        print(f'{path}: {n}x {old[:50]!r}')
        if n != must:
            raise SystemExit(f'MISMATCH in {path}: {n} != {must}')
        data = data.replace(old, new)
    with open(path, 'wb') as f:
        f.write(data)
    print(f'{path}: OK')

# php2html.h debug fopen blocks (concurrent-session debug output; breaks bare link)
p1 = r'D:\mycppos1\nefuOS\core\apps\php2html.h'
subs1 = [
    (b'''    { FILE* dbg = fopen("php_set_log.txt", "ab");
      if (dbg) { fprintf(dbg, "set %s = [%.60s]\\n", name, v.s ? v.s : "(null)"); fclose(dbg); } }
''', b'', 1),
    (b'''            { FILE* dbg = fopen("php_blocks.txt", "ab");
              if (dbg) { fprintf(dbg, "block pend=%d ob=%d cb=%d len=%d src=[%.70s]\\n", pendn, ob, cb, mlen, merged); fclose(dbg); } }
''', b'', 1),
    (b'''                        { FILE* dbg = fopen("php_blocks.txt", "ab");
                          if (dbg) { fprintf(dbg, "  stmt[%d] kind=%d name=[%.24s]\\n", i, prog.stmts[i]->kind, prog.stmts[i]->name); fclose(dbg); } }
''', b'', 1),
    (b'''                } else { FILE* dbg = fopen("php_blocks.txt", "ab");
                         if (dbg) { fprintf(dbg, "  PARSE FAILED nstmts=%d fail=%d\\n", prog.nstmts, P.fail); fclose(dbg); } }
''', b'''                } else { /* parse failed (logged via klogf below) */ }
''', 1),
]
patch(p1, subs1)

# swgl.cpp SWDBG macro -> no-op on bare
p2 = r'D:\mycppos1\nefuOS\core\apps\swgl.cpp'
subs2 = [
    (b'''#define SWDBG(...) do { FILE* dbg = fopen("D:/mycppos1/nefuOS/tools/swdbg.txt", "a"); if (dbg) { fprintf(dbg, __VA_ARGS__); fclose(dbg); } } while (0)''',
     b'''#define SWDBG(...) do { (void)0; } while (0)''', 1),
]
patch(p2, subs2)
print('ALL DONE')
