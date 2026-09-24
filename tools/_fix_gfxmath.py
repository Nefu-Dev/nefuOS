# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\gfxmath\mat4.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

# 透视：w = -z，z=-1 时 w=1
old = '''        double w = P.m[3][0] * 0 + P.m[3][1] * 0 + P.m[3][2] * (-1) + P.m[3][3] * 1;
        if (std::abs(w) > 1e-9) fails++;   // 近平面附近'''
new = '''        double w = P.m[3][0] * 0 + P.m[3][1] * 0 + P.m[3][2] * (-1) + P.m[3][3] * 1;
        if (std::abs(w - 1.0) > 1e-9) fails++;   // 透视除法分母 w = -z'''
assert s.count(old) == 1, 'persp'
s = s.replace(old, new)

# 正交：-Z 方向看，z=-50 应映射到接近 0
old2 = '''        Mat4 O = Mat4::ortho(-1, 1, -1, 1, 0.1, 100);
        Vec3 c = O.transform(Vec3(0, 0, 50));
        if (c.z > 1.001 || c.z < -1.001) fails++;'''
new2 = '''        Mat4 O = Mat4::ortho(-1, 1, -1, 1, 0.1, 100);
        Vec3 c = O.transform(Vec3(0, 0, -50));   // 视锥中点的深度应映射到 0 附近
        if (c.z > 0.5 || c.z < -0.5) fails++;'''
assert s.count(old2) == 1, 'ortho'
s = s.replace(old2, new2)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK mat4.cpp')
