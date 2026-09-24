// nefuOS simlib —— Perlin 噪声 perlin
// 教学版：Ken Perlin 经典梯度噪声，用于程序化地形/纹理/动画。
// 实现要点：晶格梯度 + 平滑插值（fade 曲线）+ 多倍频叠加（fractal）。
#pragma once
#include <vector>
#include <string>

namespace nefu {
namespace simx {

// 2D Perlin 噪声生成器
class PerlinNoise {
public:
    // 构造：随机种子
    PerlinNoise(unsigned seed);

    // 单倍频噪声：输入 (x, y)，输出 [0,1]
    double noise(double x, double y) const;
    // 分形噪声（多倍频叠加）：octaves 层，persistence 幅度衰减，输出 [0,1]
    double fractal(double x, double y, int octaves, double persistence) const;

    // 生成一个 w x h 的高度图（值 [0,1]），可指定分形参数
    // 返回按行存储的 w*h 个值
    std::vector<double> height_map(int w, int h, int octaves, double persistence) const;

    // 依据阈值把高度图转为字符画（'.' ':' '*' '@' 等）
    static std::string map_to_text(const std::vector<double>& m, int w, int h);

    // ---- self test ----
    static int self_test();

private:
    // 取晶格 (ix, iy) 的梯度编号（0~255）
    int grad_noise(int ix, int iy) const;
    // 渐变插值
    double fade(double t) const { return t * t * t * (t * (t * 6 - 15) + 10); }
    double lerp(double a, double b, double t) const { return a + t * (b - a); }

    unsigned char p[512];   // 置换表（2x 避免取模）
};

} // namespace simx
} // namespace nefu
