import re, base64
s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
for m in re.finditer(r'atob\("([A-Za-z0-9+/=]+)"\)', s):
    try:
        print(repr(base64.b64decode(m.group(1)).decode('utf-8', 'replace')))
    except Exception as e:
        print('ERR', e)
i = s.find('Vj=')
if i >= 0:
    print('Vj def ctx:', repr(s[i-30:i+500]))
