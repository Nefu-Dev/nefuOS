s = open(r'D:\mycppos1\nefuOS\verify_out\bloxd_main.js', encoding='utf-8', errors='replace').read()
print(repr(s[:300]))
print('---')
print('has use strict:', 'use strict' in s[:5000] or '"use strict"' in s[:5000])
