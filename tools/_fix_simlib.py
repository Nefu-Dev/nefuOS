# -*- coding: utf-8 -*-
import io

p = r'D:\mycppos1\nefuOS\core\simlib\world.cpp'
with io.open(p, encoding='utf-8') as f: s = f.read()
old = '''        wd.set_cell(8, 3, CELL_FOOD);
        if (!wd.add_agent(2, 3)) fails++;
        wd.step(); wd.step(); wd.step(); wd.step();
        if (wd.food_count() != 0) fails++;           // 食物被吃掉
        if (wd.total_energy() < 50) fails++;         // 吃到的能量足以维持'''
new = '''        wd.set_cell(8, 3, CELL_FOOD);
        if (!wd.add_agent(2, 3)) fails++;
        for (int i = 0; i < 8; i++) wd.step();       // 足够步数走到食物
        if (wd.food_count() != 0) fails++;           // 食物被吃掉
        if (wd.total_energy() < 50) fails++;         // 吃到的能量足以维持'''
assert s.count(old) == 1, 'world'
s = s.replace(old, new)
with io.open(p, 'w', encoding='utf-8', newline='\n') as f: f.write(s)
print('OK world.cpp')
