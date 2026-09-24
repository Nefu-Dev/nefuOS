// nefuOS simlib —— Langton 蚂蚁 langton
// 教学版：经典二维元胞自动机"朗顿蚂蚁"。
// 规则：黑格 -> 右转 90 度并翻白；白格 -> 左转 90 度并翻黑。
// 长期行为会自发产生"高速公路"结构，是简单规则涌现复杂性的经典例子。
#pragma once
#include <vector>
#include <string>

namespace nefu {
namespace simx {

// Langton 蚂蚁模拟器
class LangtonAnt {
public:
    // 构造：w x h 全白网格，蚂蚁从中心出发朝上
    LangtonAnt(int w, int h);

    // 翻转 (x, y) 格子颜色（0 白 / 1 黑）
    void set(int x, int y, int black);
    int  get(int x, int y) const;

    // 推进一步：按规则移动并翻转格子
    // 返回 false 表示蚂蚁走出边界（结束）
    bool step();

    // 当前蚂蚁位置/方向
    int  ant_x() const { return ax; }
    int  ant_y() const { return ay; }
    int  ant_dir() const { return dir; }   // 0上 1右 2下 3左

    // 黑格数量
    int black_count() const;
    // 总步数
    int steps() const { return total_steps; }

    // 文本显示：B 黑格 / . 白格 / A 蚂蚁
    std::string to_text() const;

    // 连续走 n 步，返回实际走的步数（遇边界提前停止）
    int run(int n);

    // ---- self test ----
    static int self_test();

private:
    int w, h;
    std::vector<std::vector<int> > grid;   // 0 白 / 1 黑
    int ax, ay, dir;
    int total_steps;
};

} // namespace simx
} // namespace nefu
