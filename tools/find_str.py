import sys

path = sys.argv[1]
needle = sys.argv[2].encode('ascii')
with open(path, 'rb') as f:
    data = f.read()
idx = data.find(needle)
print('found at 0x%X' % idx)
# print surrounding bytes (for .rodata, string then padding to next section)
print('prev 32:', data[max(0,idx-32):idx].hex())
print('after 32:', data[idx:idx+64].hex())
