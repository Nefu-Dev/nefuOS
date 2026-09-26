p = r'D:\mycppos1\nefuOS\core\apps\minijs.cpp'
s = open(p, encoding='utf-8').read()
lines = s.split('\n')

# find getContext function bounds
start = None
for i, l in enumerate(lines):
    if 'static JSValue nfu_getContext(' in l:
        start = i
        break
assert start is not None, 'getContext not found'
# find its end: the line '}' after a 'return g;' within the function
end = None
depth = 0
for i in range(start, len(lines)):
    d = lines[i].count('{') - lines[i].count('}')
    depth += d
    if depth == 0 and i > start:
        end = i
        break
assert end is not None, 'end not found'

# find the gl_emit line inside getContext
gline = None
for i in range(start, end + 1):
    if 'gl_emit(rc, "ctx", cid, "");' in lines[i]:
        gline = i
        break
assert gline is not None, 'gl_emit line not found'

# replace [gline, end] with a call to make_gl_obj
newbody = ['    gl_emit(rc, "ctx", cid, "");', '    return make_gl_obj(c, cid);', '}']
new_lines = lines[:start]
# insert a forward declaration before the function
new_lines.append('static JSValue make_gl_obj(JSContext* c, const char* cid);   // defined below')
new_lines.extend(lines[start:gline])
new_lines.extend(newbody)
new_lines.extend(lines[end + 1:])
open(p, 'w', encoding='utf-8', newline='').write('\n'.join(new_lines))
print('getContext body replaced, forward decl added')
