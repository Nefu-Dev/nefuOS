// nefuOS simlib —— 康威生命游戏 life
// 教学版：二维网格细胞自动机，邻居计数 + 生死规则，class / STL / 中文注释。
#pragma once
#include <vector>
#include <string>

namespace nefu {
namespace simx {

// 生命游戏细胞自动机（教学版：矩形网格，Moore 邻居）
class Life {
public:
    // 构造：w x h 空白网格
    Life(int w, int h);

    // 在 (x, y) 设置细胞状态（1 活 / 0 死）
    void set(int x, int y, int v);
    // 查询 (x, y) 状态
    int  get(int x, int y) const;

    // 推进一代：标准 Conway 规则
    //  活细胞：邻居 2~3 存活，否则死亡
    //  死细胞：邻居恰 3 则诞生
    void step();

    // 统计活细胞总数
    int  alive_count() const;
    // 网格是否全空（所有细胞死亡）
    bool is_empty() const;

    // 序列化当前网格为文本（'#' 活 / '.' 死，每行 w 字符）
    std::string to_text() const;

    // 从文本载入网格（支持 '#'/'*' 视为活）
    bool from_text(const std::string& s);

    int width() const { return w; }
    int height() const { return h; }

    // ---- self test ----
    static int self_test();

private:
    // 计算 (x, y) 的 Moore 邻居活细胞数（环绕边界）
    int neighbor_count(int x, int y) const;

    int w, h;
    std::vector<std::vector<int> > g;   // 当前状态
    std::vector<std::vector<int> > ng;  // 下一代缓冲
};

} // namespace simx
} // namespace nefu
