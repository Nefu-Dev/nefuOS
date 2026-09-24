// nefuOS simlib —— 2D 格子世界实现 + 自测
#include "simlib/world.h"
#include <cstdio>

namespace nefu {
namespace simx {

World2D::World2D(int w_, int h_) : w(w_), h(h_) {
    cells.assign(h, std::vector<int>(w, CELL_EMPTY));
    // 四周围墙
    for (int x = 0; x < w; x++) { cells[0][x] = CELL_WALL; cells[h - 1][x] = CELL_WALL; }
    for (int y = 0; y < h; y++) { cells[y][0] = CELL_WALL; cells[y][w - 1] = CELL_WALL; }
}

void World2D::set_cell(int x, int y, int type) {
    if (x <= 0 || y <= 0 || x >= w - 1 || y >= h - 1) return;  // 保护围墙
    cells[y][x] = type;
}

int World2D::cell_type(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return CELL_WALL;
    return cells[y][x];
}

bool World2D::add_agent(int x, int y) {
    if (x <= 0 || y <= 0 || x >= w - 1 || y >= h - 1) return false;
    if (cells[y][x] == CELL_WALL) return false;
    Agent2D a;
    a.x = x; a.y = y;
    agents.push_back(a);
    return true;
}

int World2D::find_food_dir(int x, int y) const {
    // 曼哈顿距离优先，逐圈搜索（教学版：半径 1~max 的菱形扫描）
    int maxr = (w > h ? w : h);
    for (int r = 1; r <= maxr; r++) {
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::abs(dx) + std::abs(dy) != r) continue;   // 菱形边界
                int nx = x + dx, ny = y + dy;
                if (cell_type(nx, ny) == CELL_FOOD) {
                    // 返回一步可达的方向
                    if (dx < 0) return 2;   // 左
                    if (dx > 0) return 1;   // 右
                    if (dy < 0) return 0;   // 上
                    return 3;               // 下
                }
            }
    }
    return -1;
}

bool World2D::try_move(Agent2D& a, int nx, int ny) {
    if (cell_type(nx, ny) == CELL_WALL) return false;
    a.x = nx; a.y = ny;
    a.step_count++;
    a.energy--;
    if (cell_type(nx, ny) == CELL_FOOD) {
        a.energy += 10;             // 吃到食物补能
        cells[ny][nx] = CELL_EMPTY; // 食物被吃掉
    }
    return true;
}

void World2D::step() {
    // 方向增量表（dir 0~3）
    static const int dx[4] = { 0, 1, 0, -1 };
    static const int dy[4] = { -1, 0, 1, 0 };
    std::vector<Agent2D> alive;
    for (size_t i = 0; i < agents.size(); i++) {
        Agent2D a = agents[i];
        if (a.energy <= 0) continue;   // 饿死
        int fd = find_food_dir(a.x, a.y);
        int d = a.dir;
        if (fd >= 0) d = fd;           // 有食物：转向食物
        int nx = a.x + dx[d];
        int ny = a.y + dy[d];
        if (!try_move(a, nx, ny)) {
            // 撞墙：尝试左右转向
            int d1 = (d + 1) % 4, d2 = (d + 3) % 4;
            if (try_move(a, a.x + dx[d1], a.y + dy[d1])) { a.dir = d1; }
            else if (try_move(a, a.x + dx[d2], a.y + dy[d2])) { a.dir = d2; }
            else { a.dir = (d + 1) % 4; }   // 都撞墙：原地转向
        } else {
            a.dir = d;
        }
        alive.push_back(a);
    }
    agents = alive;
}

int World2D::agent_count() const { return (int)agents.size(); }

int World2D::food_count() const {
    int c = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (cells[y][x] == CELL_FOOD) c++;
    return c;
}

int World2D::total_energy() const {
    int e = 0;
    for (size_t i = 0; i < agents.size(); i++) e += agents[i].energy;
    return e;
}

std::string World2D::to_text() const {
    std::string s;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            bool has_agent = false;
            for (size_t i = 0; i < agents.size(); i++)
                if (agents[i].x == x && agents[i].y == y) has_agent = true;
            char c = '.';
            if (has_agent) c = 'A';
            else if (cells[y][x] == CELL_WALL) c = '#';
            else if (cells[y][x] == CELL_FOOD) c = 'F';
            else if (cells[y][x] == CELL_HOME) c = 'H';
            s.push_back(c);
        }
        if (y + 1 < h) s.push_back('\n');
    }
    return s;
}

// ---- self test ----
int World2D::self_test() {
    int fails = 0;
    // 1. 围墙与内部放置
    {
        World2D wd(8, 6);
        if (wd.cell_type(0, 0) != CELL_WALL) fails++;
        if (wd.cell_type(7, 5) != CELL_WALL) fails++;
        if (wd.cell_type(3, 3) != CELL_EMPTY) fails++;
        wd.set_cell(4, 4, CELL_FOOD);
        if (wd.cell_type(4, 4) != CELL_FOOD) fails++;
        if (wd.food_count() != 1) fails++;
    }
    // 2. 智能体觅食：食物在右侧，智能体两步内吃到
    {
        World2D wd(10, 6);
        wd.set_cell(8, 3, CELL_FOOD);
        if (!wd.add_agent(2, 3)) fails++;
        for (int i = 0; i < 8; i++) wd.step();       // 足够步数走到食物
        if (wd.food_count() != 0) fails++;           // 食物被吃掉
        if (wd.total_energy() < 50) fails++;         // 吃到的能量足以维持
    }
    // 3. 能量耗尽死亡
    {
        World2D wd(5, 5);   // 内部只有 3x3，无食物
        if (!wd.add_agent(2, 2)) fails++;
        // 不断游走，能量应下降且最终饿死
        for (int i = 0; i < 200; i++) wd.step();
        if (wd.agent_count() != 0) fails++;
    }
    // 4. 文本渲染
    {
        World2D wd(5, 5);
        wd.add_agent(2, 2);
        std::string t = wd.to_text();
        if (t.find('A') == std::string::npos) fails++;
        if (t.find('#') == std::string::npos) fails++;   // 围墙
    }
    return fails;
}

} // namespace simx
} // namespace nefu
