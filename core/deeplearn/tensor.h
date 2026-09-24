// nefuOS 深度学习库 —— 多维张量（Q16.16 定点，无 FPU）
// 设计：
//   * Tensor 是一个多维数组句柄，data 为行主序扁平缓冲（new[] 路由到 kalloc）；
//   * 元素类型 nefu::fx::fix（int32 Q16.16），所有"浮点"运算走整数 / fx 库；
//   * 支持任意维形状（<= MAX_DIM），reshape / transpose / broadcast / elementwise / matmul；
//   * 每个张量可携带梯度缓冲 grad，并挂一个反向节点 FnNode（见 autograd.h）。
// 内存：禁止 STL，用 new[]/delete[]；深拷贝语义（拷贝构造禁用，仅移动）。
#pragma once
#include <stdint.h>
#include "../lib/softmath.h"
#include "../klib/klib.h"

namespace nefu {
namespace deeplearn {

using fx::fix;
using fx::fix64;
using fx::itofix;

// 近似判据：两个定点数是否在绝对容差 tol 内相等
inline bool fx_close(fix a, fix b, fix tol) {
    fix d = a - b;
    if (d < 0) d = -d;
    return d <= tol;
}

// 前向声明：反向计算节点（autograd.cpp 定义）
struct FnNode;

// ---------------- 多维张量 ----------------
struct Tensor {
    static const int MAX_DIM = 8;

    int   nd = 0;              // 维数
    int   shape[MAX_DIM];      // 每维大小
    int   size = 0;            // 元素总数 = 各维乘积
    fix*  data = nullptr;      // size 个元素，行主序
    fix*  grad = nullptr;      // size 个元素（requires_grad 时分配）
    bool  req_grad = false;    // 是否需要梯度
    FnNode* fn = nullptr;      // 反向节点（叶子为 nullptr）

    Tensor();
    // 移动语义（禁止拷贝，避免隐式深拷贝开销）
    Tensor(Tensor&& o);
    Tensor& operator=(Tensor&& o);
    Tensor(const Tensor&) = delete;
    Tensor& operator=(const Tensor&) = delete;
    ~Tensor();

    // 分配 grad 缓冲（若尚未分配）
    void alloc_grad();
    // 把 grad 清零（用朴素字节循环，防止 -O2 误判成 memset 栈崩溃）
    void zero_grad();
    // 按 (i0,i1,...) 索引取值（越界返回 0）
    fix&  at(const int* idx);
    fix   at(const int* idx) const;
    // 展平索引下标 -> 行主序偏移
    int    offset(const int* idx) const;

    bool valid() const { return nd >= 1 && size > 0 && data != nullptr; }
    int    dim(int i) const {
        if (nd == 0) return 0;
        if (i < 0) i = 0;
        if (i >= nd) i = nd - 1;
        return shape[i];
    }
};

// ---------------- 形状工具 ----------------
// 计算形状乘积
int t_numel(int nd, const int* shape);
// 把形状打印到 buf（调试用，ksprintf 不支持 %f，这里只打整数）
void t_shape_str(const Tensor& t, char* buf, int bufsz);

// ---------------- 构造工厂 ----------------
// 全零 / 全一 / 全 v
Tensor t_zeros(int nd, const int* shape);
Tensor t_ones(int nd, const int* shape);
Tensor t_full(int nd, const int* shape, fix v);
// 用扁平数组拷贝构造（data_in 长度必须 == size）
Tensor t_from_flat(int nd, const int* shape, const fix* data_in);
// 确定性 [lo,hi) 均匀定点随机（Xavier 风格调用方自己传范围）
Tensor t_rand(int nd, const int* shape, uint32_t seed, fix lo, fix hi);
// 深拷贝数据得到一个叶子张量（不共享存储）
Tensor t_copy(const Tensor& o);

// ---------------- 形状变换 ----------------
// 重排形状（元素总数不变），保持数据连续；反向时把梯度加回原形状
Tensor t_reshape(const Tensor& a, int nd, const int* newshape);
// 二维转置 [m,n]->[n,m]
Tensor t_transpose2d(const Tensor& a);
// 扩展（broadcast）到目标形状：仅支持某维为 1 或相等的情形
Tensor t_broadcast_to(const Tensor& a, int nd, const int* shape);
// 压扁成一维
Tensor t_flatten(const Tensor& a);

// ---------------- elementwise（支持广播：某维为 1） ----------------
Tensor t_add(const Tensor& a, const Tensor& b);
Tensor t_sub(const Tensor& a, const Tensor& b);
Tensor t_mul(const Tensor& a, const Tensor& b);        // 哈达玛积
Tensor t_div(const Tensor& a, const Tensor& b);         // 逐元素除（广播）
Tensor t_scalar(const Tensor& a, fix s);               // 标量乘
Tensor t_neg(const Tensor& a);
Tensor t_pow2(const Tensor& a);                        // 平方（用于 MSE 等）
Tensor t_abs(const Tensor& a);                          // 逐元素绝对值
Tensor t_exp(const Tensor& a);                          // 逐元素 e^x
Tensor t_log(const Tensor& a);                          // 逐元素 ln(x)（x>0）
Tensor t_clip(const Tensor& a, fix lo, fix hi);         // 截断到 [lo,hi]
Tensor t_maximum(const Tensor& a, const Tensor& b);     // 逐元素取大（广播）
Tensor t_mean_all(const Tensor& a);                    // 全体均值 -> 标量

// ---------------- 归约 ----------------
// 沿 dim 求和，输出维数 nd-1；keep_dim=false 时去掉该维
Tensor t_sum(const Tensor& a, int dim);
Tensor t_mean(const Tensor& a, int dim);
// 对所有元素求和 -> 标量（0 维张量 size=1）
Tensor t_sum_all(const Tensor& a);

// ---------------- 矩阵乘 ----------------
// a: [M,K], b: [K,N] -> [M,N]（2D）。使用 int64 累加防溢出。
Tensor t_matmul(const Tensor& a, const Tensor& b);

// ---------------- 数值梯度检查工具 ----------------
// 用对称差分近似梯度：对 t.data[i] 加/减 eps，比较 f 变化。
// f() 是一个返回标量张量（size=1）的可调用对象；这里用 C 风格函数指针 + 上下文。
// 返回近似梯度写入 out_grad（长度 t.size）。由调用方分配。
void t_numerical_grad(const Tensor& t, fix eps,
                      fix (*f)(void* ctx), void* ctx, fix* out_grad);

// ---------------- 自检 ----------------
// 验证：构造/reshape/transpose/add/matmul/sum 的已知结果。
// ---------------- 形状/切片扩展 ----------------
Tensor t_concat(const Tensor& a, const Tensor& b, int dim);
Tensor t_slice_row(const Tensor& a, int row);
void t_argmax_row(const Tensor& a, int* out);
Tensor t_max(const Tensor& a, int dim);
// 逐元素 sigmoid / tanh（带反向）
Tensor t_sigmoid(const Tensor& a);
Tensor t_tanh(const Tensor& a);
// 每行 softmax（2D [N,C] -> [N,C]），带反向
Tensor t_softmax_row(const Tensor& a);

int tensor_self_test();

} // namespace deeplearn
} // namespace nefu
