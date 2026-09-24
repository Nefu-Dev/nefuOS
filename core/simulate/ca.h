// nefuOS 仿真引擎库 —— 元胞自动机(Cellular Automata)
// 包含：
//   - Conway's Game of Life (2D 方形网格，环形/固定边界)
//   - 初等元胞自动机 (Wolfram 规则 0..255，重点 Rule 30/90/110)
//   - Langton's Ant (兰顿蚂蚁)
//   - Brian's Brain (布莱恩大脑)
//   - Seeds (种子生长)
//   - Wireworld (导线世界)
//   - Day & Night (日与夜，Life 的变体)
//   - 六边形网格 CA 简化 (蜂巢邻居)
//   - 多状态 CA (通用 k-状态邻居计数)
// 全部使用 double 友好的整型/浮点存储，无 STL、无异常、无 RTTI。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace simulate {

// ============================================================
// 边界模式
// ============================================================
enum class Boundary {
    Fixed,    // 边界外永远视为死 (0)
    Torus     // 环形：左右/上下相连
};

// ============================================================
// 1) 初等元胞自动机 (Elementary CA, Wolfram)
//    一维网格，每代由 rule (0..255) 决定：
//    邻居三元组 (left,me,right) -> 0..7 的索引，取 rule 的第 bit 位作为新值
// ============================================================
struct ElementaryCA {
    int   width;        // 网格宽度(单元数)
    uint8_t rule;       // 规则号 0..255
    Boundary boundary;  // 边界模式
    uint8_t* cell;      // width 个单元，0/1
    uint8_t* next;      // 双缓冲

    void init(int w, uint8_t r, Boundary b);
    void shutdown();
    void set_cell(int x, uint8_t v);
    uint8_t get_cell(int x) const;
    void step();                 // 推进一代
    void fill_single_seed();      // 正中一个 1，其余 0
    int  count_live() const;
};

// ============================================================
// 2) Conway's Game of Life (2D 方形网格)
//    规则：活细胞邻居 2/3 存活；死细胞邻居 3 复活
// ============================================================
struct GameOfLife {
    int   w, h;
    Boundary boundary;
    uint8_t* cell;      // w*h，0/1
    uint8_t* next;

    void init(int w, int h, Boundary b);
    void shutdown();
    void set(int x, int y, uint8_t v);
    uint8_t get(int x, int y) const;
    int  neighbors(int x, int y) const;   // 8 邻居活细胞数
    void step();
    void clear();
    int  count_live() const;
    void glider();                  // 放置一个滑翔机
    void blinker();                 // 放置一个闪烁振荡器(横向 3 连)
};

// ============================================================
// 3) Day & Night —— Life 的外生死规则变体 (B3678/S34678)
// ============================================================
struct DayNightCA {
    int   w, h;
    Boundary boundary;
    uint8_t* cell;
    uint8_t* next;

    void init(int w, int h, Boundary b);
    void shutdown();
    void set(int x, int y, uint8_t v);
    uint8_t get(int x, int y) const;
    int  neighbors(int x, int y) const;
    void step();
    int  count_live() const;
};

// ============================================================
// 4) Brian's Brain —— 三态：0=off, 1=firing, 2=refractory
//    off 邻居 firing==2 -> firing；firing -> refractory；refractory -> off
// ============================================================
struct BrianBrain {
    int   w, h;
    Boundary boundary;
    uint8_t* cell;      // 0/1/2
    uint8_t* next;

    void init(int w, int h, Boundary b);
    void shutdown();
    void set(int x, int y, uint8_t v);
    uint8_t get(int x, int y) const;
    int  firing_neighbors(int x, int y) const;
    void step();
};

// ============================================================
// 5) Seeds —— 三态：0=dead,1=alive,2=dying
//    dead 邻居 alive==2 -> alive；alive -> dying；dying -> dead
// ============================================================
struct SeedsCA {
    int   w, h;
    Boundary boundary;
    uint8_t* cell;
    uint8_t* next;

    void init(int w, int h, Boundary b);
    void shutdown();
    void set(int x, int y, uint8_t v);
    uint8_t get(int x, int y) const;
    int  alive_neighbors(int x, int y) const;
    void step();
};

// ============================================================
// 6) Wireworld —— 四态：0=empty,1=electron_head,2=electron_tail,3=conductor
//    head -> tail；tail -> conductor；conductor 邻居 head==1或2 -> head
// ============================================================
struct Wireworld {
    int   w, h;
    Boundary boundary;
    uint8_t* cell;      // 0/1/2/3
    uint8_t* next;

    void init(int w, int h, Boundary b);
    void shutdown();
    void set(int x, int y, uint8_t v);
    uint8_t get(int x, int y) const;
    int  head_neighbors(int x, int y) const;
    void step();
    int  count_state(uint8_t s) const;
};

// ============================================================
// 7) Langton's Ant —— 兰顿蚂蚁
//    网格：0=白,1=黑；蚂蚁面朝四方向之一
//    白格：右转 90°，反色，前进一步
//    黑格：左转 90°，反色，前进一步
// ============================================================
struct LangtonAnt {
    int   w, h;
    Boundary boundary;
    uint8_t* cell;      // 0/1
    int   ax, ay;       // 蚂蚁位置
    int   dir;          // 0=右 1=下 2=左 3=上
    uint64_t steps_done;

    void init(int w, int h, int sx, int sy);
    void shutdown();
    void step();                // 蚂蚁走一步
    uint8_t get(int x, int y) const;
    int  colored_count() const;
};

// ============================================================
// 8) 六边形网格 CA (简化：offset 坐标的蜂巢邻居)
//    使用奇数行偏移的"奇-R"布局：6 个邻居
// ============================================================
struct HexCA {
    int   w, h;
    uint8_t* cell;
    uint8_t* next;

    void init(int w, int h);
    void shutdown();
    void set(int x, int y, uint8_t v);
    uint8_t get(int x, int y) const;
    int  neighbors(int x, int y) const;   // 6 邻居
    void step();                          // Life 规则跑在六边形上
    int  count_live() const;
};

// ============================================================
// 9) 通用多状态 CA —— 由用户给出生存/出生邻域集合
//    例如 Life: survive={2,3}, birth={3}
// ============================================================
struct MultiStateCA {
    int   w, h;
    Boundary boundary;
    uint8_t* cell;
    uint8_t* next;
    bool   surv[9];     // survive[n]：邻居数 n 时活细胞是否存活
    bool   birth[9];    // birth[n]：邻居数 n 时死细胞是否复活

    void init(int w, int h, Boundary b);
    void shutdown();
    void set_rules(const int* surv, int ns, const int* birth, int nb);
    void set(int x, int y, uint8_t v);
    uint8_t get(int x, int y) const;
    int  neighbors(int x, int y) const;
    void step();
    int  count_live() const;
};

// ============================================================
// 自检：返回失败数 (0 == 全部通过)
// ============================================================
int ca_self_test();

} // namespace simulate
} // namespace nefu
