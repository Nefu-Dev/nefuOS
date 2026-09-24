# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\audlib\mixer.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()

old = '''    for (int i = 0; i < in.frames(); i++) {
        double x = in.frame(i);
        if (i >= d1) b1[i] = x + b1[i - d1] * damping;
        if (i >= d2) b2[i] = x + b2[i - d2] * damping;
        if (i >= d3) b3[i] = x + b3[i - d3] * damping;
        if (i >= d4) b4[i] = x + b4[i - d4] * damping;
        double wet = (b1[i] + b2[i] + b3[i] + b4[i]) * 0.25;
        double v = x * 0.5 + wet * 0.5;'''
new = '''    for (int i = 0; i < in.frames(); i++) {
        double x = in.frame(i);
        // 梳状滤波：输出 = 输入延迟 d 帧 + 反馈
        if (i >= d1) b1[i] = in.frame(i - d1) + b1[i - d1] * damping;
        if (i >= d2) b2[i] = in.frame(i - d2) + b2[i - d2] * damping;
        if (i >= d3) b3[i] = in.frame(i - d3) + b3[i - d3] * damping;
        if (i >= d4) b4[i] = in.frame(i - d4) + b4[i - d4] * damping;
        double wet = (b1[i] + b2[i] + b3[i] + b4[i]) * 0.25;
        double v = x * 0.5 + wet * 0.5;'''
assert s.count(old) == 1, 'reverb'
s = s.replace(old, new)

with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK reverb')
