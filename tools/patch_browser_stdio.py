p = r'D:\mycppos1\nefuOS\core\apps\browser.cpp'
with open(p, 'rb') as f:
    data = f.read()
old = (
    b'                int clen = nfuphp::php_convert(j->php_buf, conv, conv_cap);\n'
    b'                { FILE* dbg = fopen("php_conv_out.txt", "wb");\n'
    b'                  if (dbg) { fwrite(conv, 1, clen < 1400 ? (size_t)clen : 1400, dbg); fclose(dbg); } }\n'
    b'                klogf("browser: php converted %d -> %d bytes", j->php_len, clen);'
)
new = (
    b'                int clen = nfuphp::php_convert(j->php_buf, conv, conv_cap);\n'
    b'                klogf("browser: php converted %d -> %d bytes", j->php_len, clen);'
)
n = data.count(old)
print('occurrences:', n)
if n != 1:
    raise SystemExit(1)
data = data.replace(old, new)
with open(p, 'wb') as f:
    f.write(data)
print('patched OK')
