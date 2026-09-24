// nefuOS simlib —— 2D 格子世界 world
// 教学版：智能体在网格上觅食/游走的小型生态模拟。
// 用于理解"基于代理的建模（ABM）"：空间 + 状态 + 行为规则。
#pragma once
#include <vector>
#include <string>

namespace nefu {
namespace simx {

// 格子内容物类型
enum CellType {
    CELL_EMPTY = 0,
    CELL_WALL,     // 障碍
    CELL_FOOD,     // 食物
    CELL_HOME      // 巢穴/起点
};

// 单个智能体（agent）
struct Agent2D {
    int x, y;        // 当前位置
    int dir;         // 方向 0~3：0上 1右 2下 3左
    int energy;      // 能量（吃到食物增加，移动消耗）
    int step_count;  // 累计步数
    Agent2D() : x(0), y(0), dir(0), energy(50), step_count(0) {}
};

// 2D 格子世界：智能体自主觅食模拟
class World2D {
public:
    // 构造：w x h，四周默认围墙，内部空地
    World2D(int w, int h);

    // 放置内容物
    void set_cell(int x, int y, int type);
    // 查询格子类型
    int  cell_type(int x, int y) const;
    // 放置智能体（返回是否成功）
    bool add_agent(int x, int y);

    // 推进一个时间步：每个存活智能体尝试移动
    // 规则：优先朝食物方向，否则保持前进，撞墙则转向
    void step();

    // 统计信息
    int agent_count() const;      // 存活智能体数
    int food_count() const;       // 剩余食物数
    int total_energy() const;     // 全部智能体能量和

    // 文本显示：A 智能体 / # 墙 / F 食物 / . 空地 / H 巢穴
    std::string to_text() const;

    // ---- self test ----
    static int self_test();

private:
    // 探测 (x, y) 相邻八格中最近的食物的方向（无则 -1）
    int find_food_dir(int x, int y) const;
    // 尝试移动到 (nx, ny)：撞墙返回 false
    bool try_move(Agent2D& a, int nx, int ny);

    int w, h;
    std::vector<std::vector<int> > cells;
    std::vector<Agent2D> agents;
};

} // namespace simx
} // namespace nefu
