// nefuOS 深度学习库 —— 多维张量实现
// 见 tensor.h 说明。
// 反向节点约定：输入张量以裸指针引用（调用方命名局部，生命期覆盖 backward）；
// 输出的上游梯度只存堆缓冲指针 go（Tensor move 后堆地址不变），形状标量在前向时捕获。
#pragma GCC optimize("no-tree-loop-distribute-patterns")  // 防 -O2 把清零循环优化成 memset 栈崩溃
#include "tensor.h"
#include "autograd.h"

namespace nefu {
namespace deeplearn {

// ==================== 内部小工具 ====================
namespace {

// 朴素内存清零（逐 int 写，绝不交给编译器识别成 memset）
void clear_buf(fix* p, int n) {
    if (!p) return;
    for (int i = 0; i < n; i++) p[i] = 0;
}
void copy_buf(fix* dst, const fix* src, int n) {
    if (!dst || !src) return;
    for (int i = 0; i < n; i++) dst[i] = src[i];
}
// 朴素 LCG（自包含，不依赖 ml_util）
struct DlRng {
    uint32_t s;
    DlRng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
    fix next_fx() { return (fix)(next() >> 16); }   // [0,1)
};

// 把输出坐标 out_idx[out_nd] 映射到输入坐标（处理 size-1 广播维）。
// 输入维数 in_nd <= out_nd，从右对齐比较；若某维输入为 1 则该输入坐标取 0。
void map_coord(const int* out_idx, int out_nd,
               const int* in_shape, int in_nd,
               int* in_idx) {
    for (int d = 0; d < in_nd; d++) {
        int od = out_nd - in_nd + d;     // 右对齐
        int is = in_shape[d];
        in_idx[d] = (is == 1) ? 0 : out_idx[od];
    }
}

// 行主序坐标 -> 偏移
int coord_offset(int nd, const int* shape, const int* idx) {
    int off = 0;
    int stride = 1;
    for (int d = nd - 1; d >= 0; d--) {
        off += idx[d] * stride;
        stride *= shape[d];
    }
    return off;
}

// 广播形状：右对齐，维度相等或其一为 1
bool broadcast_shape(int nda, const int* sa, int ndb, const int* sb,
                     int* out_nd, int* out_shape) {
    int nd = nda > ndb ? nda : ndb;
    *out_nd = nd;
    for (int d = 0; d < nd; d++) {
        int da = nda - 1 - d, db = ndb - 1 - d;
        int va = (da >= 0) ? sa[da] : 1;
        int vb = (db >= 0) ? sb[db] : 1;
        if (va != 1 && vb != 1 && va != vb) return false;
        out_shape[nd - 1 - d] = (va > vb) ? va : vb;
    }
    return true;
}

} // namespace

// ==================== Tensor 生命周期 ====================
Tensor::Tensor() {
    for (int i = 0; i < MAX_DIM; i++) shape[i] = 0;
}

Tensor::Tensor(Tensor&& o) {
    nd = o.nd; size = o.size; req_grad = o.req_grad; fn = o.fn;
    for (int i = 0; i < MAX_DIM; i++) shape[i] = o.shape[i];
    data = o.data; grad = o.grad;
    o.data = nullptr; o.grad = nullptr; o.fn = nullptr; o.nd = 0; o.size = 0;
}

Tensor& Tensor::operator=(Tensor&& o) {
    if (this != &o) {
        if (data) delete[] data;
        if (grad) delete[] grad;
        nd = o.nd; size = o.size; req_grad = o.req_grad; fn = o.fn;
        for (int i = 0; i < MAX_DIM; i++) shape[i] = o.shape[i];
        data = o.data; grad = o.grad;
        o.data = nullptr; o.grad = nullptr; o.fn = nullptr; o.nd = 0; o.size = 0;
    }
    return *this;
}

Tensor::~Tensor() {
    if (data) delete[] data;
    if (grad) delete[] grad;
    data = nullptr; grad = nullptr;
}

void Tensor::alloc_grad() {
    req_grad = true;
    if (grad || size <= 0) return;
    grad = new fix[size > 0 ? size : 1];
    clear_buf(grad, size);
}

void Tensor::zero_grad() {
    if (grad) clear_buf(grad, size);
}

int Tensor::offset(const int* idx) const { return coord_offset(nd, shape, idx); }
fix& Tensor::at(const int* idx) { return data[offset(idx)]; }
fix Tensor::at(const int* idx) const { return data[offset(idx)]; }

// ==================== 形状工具 ====================
int t_numel(int nd, const int* shape) {
    int s = 1;
    for (int i = 0; i < nd; i++) s *= shape[i] > 0 ? shape[i] : 0;
    return s;
}

void t_shape_str(const Tensor& t, char* buf, int bufsz) {
    int p = 0;
    p += nefu::ksprintf(buf + p, bufsz - p, "[%d", t.nd > 0 ? t.shape[0] : 0);
    for (int i = 1; i < t.nd; i++)
        p += nefu::ksprintf(buf + p, bufsz - p, "x%d", t.shape[i]);
    p += nefu::ksprintf(buf + p, bufsz - p, "]");
}

// ==================== 工厂 ====================
static Tensor t_alloc(int nd, const int* shape) {
    Tensor r;
    r.nd = nd;
    for (int i = 0; i < nd; i++) r.shape[i] = shape[i];
    r.size = t_numel(nd, shape);
    r.data = new fix[r.size > 0 ? r.size : 1];
    clear_buf(r.data, r.size);
    return r;
}

Tensor t_zeros(int nd, const int* shape) { return t_alloc(nd, shape); }

Tensor t_ones(int nd, const int* shape) {
    Tensor r = t_alloc(nd, shape);
    for (int i = 0; i < r.size; i++) r.data[i] = fx::FX_ONE;
    return r;
}

Tensor t_full(int nd, const int* shape, fix v) {
    Tensor r = t_alloc(nd, shape);
    for (int i = 0; i < r.size; i++) r.data[i] = v;
    return r;
}

Tensor t_from_flat(int nd, const int* shape, const fix* data_in) {
    Tensor r = t_alloc(nd, shape);
    copy_buf(r.data, data_in, r.size);
    return r;
}

Tensor t_rand(int nd, const int* shape, uint32_t seed, fix lo, fix hi) {
    Tensor r = t_alloc(nd, shape);
    DlRng rng(seed);
    fix span = hi - lo;
    for (int i = 0; i < r.size; i++)
        r.data[i] = lo + fx::fx_mul(rng.next_fx(), span);
    return r;
}

Tensor t_copy(const Tensor& o) {
    Tensor r = t_alloc(o.nd, o.shape);
    copy_buf(r.data, o.data, o.size);
    return r;
}

// ==================== 形状变换 ====================
struct ReshapeView : FnNode {
    Tensor* in;
    fix* go;            // 输出上游梯度（堆缓冲，size==in->size）
    int n;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) in->grad[i] += go[i];
    }
    const char* name() const override { return "reshape"; }
};

Tensor t_reshape(const Tensor& a, int nd, const int* newshape) {
    Tensor r = t_alloc(nd, newshape);
    copy_buf(r.data, a.data, a.size);
    if (a.req_grad) {
        requires_grad(r);
        ReshapeView* n = new ReshapeView();
        n->in = (Tensor*)&a; n->go = r.grad; n->n = a.size;
        r.fn = n; tape_push(n);
    }
    return r;
}

struct Transpose2dNode : FnNode {
    Tensor* in;
    fix* go;
    int M, N;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < M; i++)
            for (int j = 0; j < N; j++)
                in->grad[i * N + j] += go[j * M + i];
    }
    const char* name() const override { return "transpose2d"; }
};

Tensor t_transpose2d(const Tensor& a) {
    int M = a.shape[0], N = a.shape[1];
    int ns[2] = { N, M };
    Tensor r = t_alloc(2, ns);
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++)
            r.data[j * M + i] = a.data[i * N + j];
    if (a.req_grad) {
        requires_grad(r);
        Transpose2dNode* n = new Transpose2dNode();
        n->in = (Tensor*)&a; n->go = r.grad; n->M = M; n->N = N;
        r.fn = n; tape_push(n);
    }
    return r;
}

Tensor t_broadcast_to(const Tensor& a, int nd, const int* shape) {
    Tensor r = t_alloc(nd, shape);
    int out_idx[Tensor::MAX_DIM];
    int in_idx[Tensor::MAX_DIM];
    for (int o = 0; o < r.size; o++) {
        int t = o;
        for (int d = nd - 1; d >= 0; d--) { out_idx[d] = t % shape[d]; t /= shape[d]; }
        map_coord(out_idx, nd, a.shape, a.nd, in_idx);
        r.data[o] = a.data[coord_offset(a.nd, a.shape, in_idx)];
    }
    return r;
}

Tensor t_flatten(const Tensor& a) {
    int ns[1] = { a.size };
    return t_reshape(a, 1, ns);
}

// ==================== elementwise ====================
struct AddNode : FnNode {
    Tensor* a; Tensor* b;
    fix* go;
    int out_nd;
    int out_shape[Tensor::MAX_DIM];
    void apply() override {
        int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
        for (int o = 0; o < t_numel(out_nd, out_shape); o++) {
            int t = o;
            for (int d = out_nd - 1; d >= 0; d--) { out_idx[d] = t % out_shape[d]; t /= out_shape[d]; }
            fix g = go[o];
            if (a->grad) { map_coord(out_idx, out_nd, a->shape, a->nd, ia); a->grad[coord_offset(a->nd,a->shape,ia)] += g; }
            if (b->grad) { map_coord(out_idx, out_nd, b->shape, b->nd, ib); b->grad[coord_offset(b->nd,b->shape,ib)] += g; }
        }
    }
    const char* name() const override { return "add"; }
};

Tensor t_add(const Tensor& a, const Tensor& b) {
    int os[Tensor::MAX_DIM], ond;
    if (!broadcast_shape(a.nd, a.shape, b.nd, b.shape, &ond, os)) return Tensor();
    Tensor r = t_alloc(ond, os);
    int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
    for (int o = 0; o < r.size; o++) {
        int t = o;
        for (int d = ond - 1; d >= 0; d--) { out_idx[d] = t % os[d]; t /= os[d]; }
        map_coord(out_idx, ond, a.shape, a.nd, ia);
        map_coord(out_idx, ond, b.shape, b.nd, ib);
        r.data[o] = a.data[coord_offset(a.nd,a.shape,ia)] + b.data[coord_offset(b.nd,b.shape,ib)];
    }
    if (a.req_grad || b.req_grad) {
        requires_grad(r);
        AddNode* n = new AddNode();
        n->a = (Tensor*)&a; n->b = (Tensor*)&b; n->go = r.grad; n->out_nd = ond;
        for (int i = 0; i < ond; i++) n->out_shape[i] = os[i];
        r.fn = n; tape_push(n);
    }
    return r;
}

struct SubNode : FnNode {
    Tensor* a; Tensor* b;
    fix* go;
    int out_nd;
    int out_shape[Tensor::MAX_DIM];
    void apply() override {
        int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
        for (int o = 0; o < t_numel(out_nd, out_shape); o++) {
            int t = o;
            for (int d = out_nd - 1; d >= 0; d--) { out_idx[d] = t % out_shape[d]; t /= out_shape[d]; }
            fix g = go[o];
            if (a->grad) { map_coord(out_idx, out_nd, a->shape, a->nd, ia); a->grad[coord_offset(a->nd,a->shape,ia)] += g; }
            if (b->grad) { map_coord(out_idx, out_nd, b->shape, b->nd, ib); b->grad[coord_offset(b->nd,b->shape,ib)] -= g; }
        }
    }
    const char* name() const override { return "sub"; }
};

Tensor t_sub(const Tensor& a, const Tensor& b) {
    int os[Tensor::MAX_DIM], ond;
    if (!broadcast_shape(a.nd, a.shape, b.nd, b.shape, &ond, os)) return Tensor();
    Tensor r = t_alloc(ond, os);
    int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
    for (int o = 0; o < r.size; o++) {
        int t = o;
        for (int d = ond - 1; d >= 0; d--) { out_idx[d] = t % os[d]; t /= os[d]; }
        map_coord(out_idx, ond, a.shape, a.nd, ia);
        map_coord(out_idx, ond, b.shape, b.nd, ib);
        r.data[o] = a.data[coord_offset(a.nd,a.shape,ia)] - b.data[coord_offset(b.nd,b.shape,ib)];
    }
    if (a.req_grad || b.req_grad) {
        requires_grad(r);
        SubNode* n = new SubNode();
        n->a = (Tensor*)&a; n->b = (Tensor*)&b; n->go = r.grad; n->out_nd = ond;
        for (int i = 0; i < ond; i++) n->out_shape[i] = os[i];
        r.fn = n; tape_push(n);
    }
    return r;
}

struct MulNode : FnNode {
    Tensor* a; Tensor* b;
    fix* go;
    int out_nd;
    int out_shape[Tensor::MAX_DIM];
    void apply() override {
        int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
        for (int o = 0; o < t_numel(out_nd, out_shape); o++) {
            int t = o;
            for (int d = out_nd - 1; d >= 0; d--) { out_idx[d] = t % out_shape[d]; t /= out_shape[d]; }
            map_coord(out_idx, out_nd, a->shape, a->nd, ia);
            map_coord(out_idx, out_nd, b->shape, b->nd, ib);
            int ao = coord_offset(a->nd,a->shape,ia), bo = coord_offset(b->nd,b->shape,ib);
            fix g = go[o];
            if (a->grad) a->grad[ao] += fx::fx_mul(b->data[bo], g);
            if (b->grad) b->grad[bo] += fx::fx_mul(a->data[ao], g);
        }
    }
    const char* name() const override { return "mul"; }
};

Tensor t_mul(const Tensor& a, const Tensor& b) {
    int os[Tensor::MAX_DIM], ond;
    if (!broadcast_shape(a.nd, a.shape, b.nd, b.shape, &ond, os)) return Tensor();
    Tensor r = t_alloc(ond, os);
    int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
    for (int o = 0; o < r.size; o++) {
        int t = o;
        for (int d = ond - 1; d >= 0; d--) { out_idx[d] = t % os[d]; t /= os[d]; }
        map_coord(out_idx, ond, a.shape, a.nd, ia);
        map_coord(out_idx, ond, b.shape, b.nd, ib);
        r.data[o] = fx::fx_mul(a.data[coord_offset(a.nd,a.shape,ia)],
                               b.data[coord_offset(b.nd,b.shape,ib)]);
    }
    if (a.req_grad || b.req_grad) {
        requires_grad(r);
        MulNode* n = new MulNode();
        n->a = (Tensor*)&a; n->b = (Tensor*)&b; n->go = r.grad; n->out_nd = ond;
        for (int i = 0; i < ond; i++) n->out_shape[i] = os[i];
        r.fn = n; tape_push(n);
    }
    return r;
}

struct ScalarNode : FnNode {
    Tensor* in;
    fix* go;
    int n;
    fix s;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) in->grad[i] += fx::fx_mul(go[i], s);
    }
    const char* name() const override { return "scalar"; }
};

Tensor t_scalar(const Tensor& a, fix s) {
    Tensor r = t_alloc(a.nd, a.shape);
    for (int i = 0; i < a.size; i++) r.data[i] = fx::fx_mul(a.data[i], s);
    if (a.req_grad) {
        requires_grad(r);
        ScalarNode* n = new ScalarNode();
        n->in = (Tensor*)&a; n->go = r.grad; n->n = a.size; n->s = s;
        r.fn = n; tape_push(n);
    }
    return r;
}

Tensor t_neg(const Tensor& a) { return t_scalar(a, (fix)-fx::FX_ONE); }

struct Pow2Node : FnNode {
    Tensor* in;
    fix* go;
    int n;
    void apply() override {
        if (!in->grad) return;
        fix two = fx::itofix(2);
        for (int i = 0; i < n; i++)
            in->grad[i] += fx::fx_mul(fx::fx_mul(two, in->data[i]), go[i]);
    }
    const char* name() const override { return "pow2"; }
};

Tensor t_pow2(const Tensor& a) {
    Tensor r = t_alloc(a.nd, a.shape);
    for (int i = 0; i < a.size; i++) r.data[i] = fx::fx_mul(a.data[i], a.data[i]);
    if (a.req_grad) {
        requires_grad(r);
        Pow2Node* n = new Pow2Node();
        n->in = (Tensor*)&a; n->go = r.grad; n->n = a.size;
        r.fn = n; tape_push(n);
    }
    return r;
}

// ==================== 归约 ====================
struct SumNode : FnNode {
    Tensor* in;
    fix* go;
    int dim;
    int out_nd;
    int out_shape[Tensor::MAX_DIM];
    void apply() override {
        if (!in->grad) return;
        int nd = in->nd;
        int out_idx[Tensor::MAX_DIM], in_idx[Tensor::MAX_DIM];
        for (int o = 0; o < t_numel(out_nd, out_shape); o++) {
            int t = o;
            for (int d = out_nd - 1; d >= 0; d--) { out_idx[d] = t % out_shape[d]; t /= out_shape[d]; }
            fix g = go[o];
            for (int k = 0; k < in->shape[dim]; k++) {
                for (int d = 0, od = 0; d < nd; d++) {
                    if (d == dim) in_idx[d] = k;
                    else in_idx[d] = out_idx[od++];
                }
                in->grad[coord_offset(nd, in->shape, in_idx)] += g;
            }
        }
    }
    const char* name() const override { return "sum"; }
};

Tensor t_sum(const Tensor& a, int dim) {
    if (dim < 0) dim = a.nd + dim;
    int out_shape[Tensor::MAX_DIM];
    int on = 0;
    for (int i = 0; i < a.nd; i++) if (i != dim) out_shape[on++] = a.shape[i];
    Tensor r = t_alloc(on, out_shape);
    int out_idx[Tensor::MAX_DIM], in_idx[Tensor::MAX_DIM];
    for (int o = 0; o < r.size; o++) {
        int t = o;
        for (int d = on - 1; d >= 0; d--) { out_idx[d] = t % out_shape[d]; t /= out_shape[d]; }
        fix acc = 0;
        for (int k = 0; k < a.shape[dim]; k++) {
            for (int d = 0, od = 0; d < a.nd; d++) {
                if (d == dim) in_idx[d] = k; else in_idx[d] = out_idx[od++];
            }
            acc += a.data[coord_offset(a.nd, a.shape, in_idx)];
        }
        r.data[o] = acc;
    }
    if (a.req_grad) {
        requires_grad(r);
        SumNode* n = new SumNode();
        n->in = (Tensor*)&a; n->go = r.grad; n->dim = dim; n->out_nd = on;
        for (int i = 0; i < on; i++) n->out_shape[i] = out_shape[i];
        r.fn = n; tape_push(n);
    }
    return r;
}

Tensor t_mean(const Tensor& a, int dim) {
    Tensor s = t_sum(a, dim);
    fix n = fx::itofix(a.shape[dim]);
    Tensor r = t_scalar(s, fx::fx_div(fx::FX_ONE, n));
    return r;
}

struct SumAllNode : FnNode {
    Tensor* in;
    fix* go;
    void apply() override {
        if (!in->grad) return;
        fix g = go[0];
        for (int i = 0; i < in->size; i++) in->grad[i] += g;
    }
    const char* name() const override { return "sum_all"; }
};

Tensor t_sum_all(const Tensor& a) {
    fix acc = 0;
    for (int i = 0; i < a.size; i++) acc += a.data[i];
    int sh[1] = { 1 };
    Tensor r = t_alloc(1, sh);
    r.data[0] = acc;
    if (a.req_grad) {
        requires_grad(r);
        SumAllNode* n = new SumAllNode();
        n->in = (Tensor*)&a; n->go = r.grad;
        r.fn = n; tape_push(n);
    }
    return r;
}

// ==================== 矩阵乘 ====================
struct MatmulNode : FnNode {
    Tensor* a; Tensor* b;
    fix* go;
    int M, K, N;
    void apply() override {
        if (a->grad) {
            for (int i = 0; i < M; i++)
                for (int k = 0; k < K; k++) {
                    fix64 s = 0;
                    for (int n = 0; n < N; n++)
                        s += (fix64)go[i * N + n] * (fix64)b->data[k * N + n];
                    a->grad[i * K + k] += (fix)(s >> 16);
                }
        }
        if (b->grad) {
            for (int k = 0; k < K; k++)
                for (int n = 0; n < N; n++) {
                    fix64 s = 0;
                    for (int i = 0; i < M; i++)
                        s += (fix64)a->data[i * K + k] * (fix64)go[i * N + n];
                    b->grad[k * N + n] += (fix)(s >> 16);
                }
        }
    }
    const char* name() const override { return "matmul"; }
};

Tensor t_matmul(const Tensor& a, const Tensor& b) {
    int M = a.shape[0], K = a.shape[1], N = b.shape[1];
    int os[2] = { M, N };
    Tensor r = t_alloc(2, os);
    for (int i = 0; i < M; i++)
        for (int n = 0; n < N; n++) {
            fix64 s = 0;
            for (int k = 0; k < K; k++)
                s += (fix64)a.data[i * K + k] * (fix64)b.data[k * N + n];
            r.data[i * N + n] = (fix)(s >> 16);
        }
    if (a.req_grad || b.req_grad) {
        requires_grad(r);
        MatmulNode* n = new MatmulNode();
        n->a = (Tensor*)&a; n->b = (Tensor*)&b; n->go = r.grad;
        n->M = M; n->K = K; n->N = N;
        r.fn = n; tape_push(n);
    }
    return r;
}

// ==================== 数值梯度 ====================
void t_numerical_grad(const Tensor& t, fix eps,
                      fix (*f)(void* ctx), void* ctx, fix* out_grad) {
    for (int i = 0; i < t.size; i++) {
        fix orig = t.data[i];
        t.data[i] = orig + eps;   fix fp = f(ctx);
        t.data[i] = orig - eps;   fix fm = f(ctx);
        t.data[i] = orig;
        fix num = fp - fm;
        out_grad[i] = fx::fx_div(num, eps + eps);
    }
}

// ==================== 自检 ====================
int tensor_self_test() {
    int fails = 0;
    // 1) 形状与元素数
    {
        int sh[3] = { 2, 3, 4 };
        Tensor a = t_zeros(3, sh);
        if (a.size != 24) fails++;
        if (a.nd != 3) fails++;
    }
    // 2) matmul 单位阵
    {
        fix va[4] = { fx::itofix(1), fx::itofix(2), fx::itofix(3), fx::itofix(4) };
        int sa[2] = { 2, 2 };
        Tensor A = t_from_flat(2, sa, va);
        fix vi[4] = { fx::FX_ONE, 0, 0, fx::FX_ONE };
        Tensor I = t_from_flat(2, sa, vi);
        Tensor C = t_matmul(A, I);
        for (int i = 0; i < 4; i++)
            if (!fx_close(C.data[i], va[i], fx::fxf(1,100))) fails++;
    }
    // 3) matmul 已知值
    {
        fix va[4] = { fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4) };
        fix vb[4] = { fx::itofix(5),fx::itofix(6),fx::itofix(7),fx::itofix(8) };
        int s2[2] = { 2, 2 };
        Tensor A = t_from_flat(2, s2, va);
        Tensor B = t_from_flat(2, s2, vb);
        Tensor C = t_matmul(A, B);
        fix expect[4] = { fx::itofix(19), fx::itofix(22), fx::itofix(43), fx::itofix(50) };
        for (int i = 0; i < 4; i++)
            if (!fx_close(C.data[i], expect[i], fx::fxf(1,100))) fails++;
    }
    // 4) reshape
    {
        fix v[6] = { fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4),fx::itofix(5),fx::itofix(6) };
        int s1[1] = { 6 };
        Tensor a = t_from_flat(1, s1, v);
        int s2[2] = { 2, 3 };
        Tensor b = t_reshape(a, 2, s2);
        if (b.size != 6) fails++;
        if (!fx_close(b.data[3], fx::itofix(4), fx::fxf(1,100))) fails++;
    }
    // 5) transpose2d
    {
        fix v[4] = { fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4) };
        int s2[2] = { 2, 2 };
        Tensor a = t_from_flat(2, s2, v);
        Tensor T = t_transpose2d(a);
        if (!fx_close(T.data[1], fx::itofix(3), fx::fxf(1,100))) fails++;
        if (!fx_close(T.data[2], fx::itofix(2), fx::fxf(1,100))) fails++;
    }
    // 6) add broadcast
    {
        fix va[4] = { fx::FX_ONE,fx::FX_ONE,fx::FX_ONE,fx::FX_ONE };
        int sa[2] = { 2, 2 };
        Tensor A = t_from_flat(2, sa, va);
        fix vb[2] = { fx::itofix(1), fx::itofix(2) };
        int sb[1] = { 2 };
        Tensor B = t_from_flat(1, sb, vb);
        Tensor C = t_add(A, B);
        if (!fx_close(C.data[0], fx::itofix(2), fx::fxf(1,100))) fails++;
        if (!fx_close(C.data[1], fx::itofix(3), fx::fxf(1,100))) fails++;
    }
    // 7) sum dim
    {
        fix v[6] = { fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4),fx::itofix(5),fx::itofix(6) };
        int s2[2] = { 2, 3 };
        Tensor A = t_from_flat(2, s2, v);
        Tensor s0 = t_sum(A, 0);
        if (!fx_close(s0.data[0], fx::itofix(5), fx::fxf(1,100))) fails++;
        if (!fx_close(s0.data[2], fx::itofix(9), fx::fxf(1,100))) fails++;
    }
    // concat dim=0
    {
        fix va[4]={fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4)};
        fix vb[2]={fx::itofix(5),fx::itofix(6)};
        int sa[2]={2,2}, sb[2]={1,2};
        Tensor A=t_from_flat(2,sa,va); Tensor B=t_from_flat(2,sb,vb);
        Tensor C=t_concat(A,B,0);
        if (C.shape[0]!=3 || C.shape[1]!=2) fails++;
        if (!fx_close(C.data[4], fx::itofix(5), fx::fxf(1,100))) fails++;
    }
    // argmax
    {
        fix v[6]={fx::itofix(1),fx::itofix(5),fx::itofix(2), fx::itofix(0),fx::itofix(0),fx::itofix(9)};
        int s2[2]={2,3}; Tensor X=t_from_flat(2,s2,v);
   int out[2]; t_argmax_row(X,out);
        if (out[0]!=1) fails++;
        if (out[1]!=2) fails++;
    }
    // max over dim=0
    {
        fix v[6]={fx::itofix(1),fx::itofix(3),fx::itofix(2), fx::itofix(5),fx::itofix(2),fx::itofix(4)};
        int s2[2]={2,3}; Tensor X=t_from_flat(2,s2,v);
        Tensor M=t_max(X,0);
        if (!fx_close(M.data[0], fx::itofix(5), fx::fxf(1,100))) fails++;
        if (!fx_close(M.data[2], fx::itofix(4), fx::fxf(1,100))) fails++;
    }
    // sigmoid(0)=0.5
    {
        fix v[2]={0,0};
        Tensor X=t_from_flat(1,(int[1]){2},v);
        Tensor S=t_sigmoid(X);
        if (!fx_close(S.data[0], fx::FX_HALF, fx::fxf(1,100))) fails++;
    }
    // tanh(0)=0
    {
        fix v[1]={0};
        Tensor X=t_from_flat(1,(int[1]){1},v);
        Tensor T=t_tanh(X);
        if (!fx_close(T.data[0], 0, fx::fxf(1,100))) fails++;
    }
    // softmax_row：两行概率和为 1
    {
        fix v[6]={fx::itofix(1),fx::itofix(2),fx::itofix(3),
                  fx::itofix(1),fx::itofix(1),fx::itofix(1)};
        Tensor X=t_from_flat(2,(int[2]){2,3},v);
        Tensor S=t_softmax_row(X);
        fix r0=S.data[0]+S.data[1]+S.data[2];
        fix r1=S.data[3]+S.data[4]+S.data[5];
        if (!fx_close(r0, fx::FX_ONE, fx::fxf(2,100))) fails++;
        if (!fx_close(r1, fx::FX_ONE, fx::fxf(2,100))) fails++;
    }
    return fails;
}
// ==================== 扩展 elementwise（追加） ====================
struct DivNode : FnNode {
    Tensor* a; Tensor* b;
    fix* go;
    int out_nd;
    int out_shape[Tensor::MAX_DIM];
    void apply() override {
        int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
        for (int o = 0; o < t_numel(out_nd, out_shape); o++) {
            int t = o;
            for (int d = out_nd - 1; d >= 0; d--) { out_idx[d] = t % out_shape[d]; t /= out_shape[d]; }
            map_coord(out_idx, out_nd, a->shape, a->nd, ia);
            map_coord(out_idx, out_nd, b->shape, b->nd, ib);
            int ao = coord_offset(a->nd,a->shape,ia), bo = coord_offset(b->nd,b->shape,ib);
            fix g = go[o];
            if (a->grad) a->grad[ao] += fx::fx_div(g, b->data[bo]);
            if (b->grad) {
                fix bb = fx::fx_mul(b->data[bo], b->data[bo]);
                b->grad[bo] -= fx::fx_div(fx::fx_mul(a->data[ao], g), bb);
            }
        }
    }
    const char* name() const override { return "div"; }
};

Tensor t_div(const Tensor& a, const Tensor& b) {
    int os[Tensor::MAX_DIM], ond;
    if (!broadcast_shape(a.nd, a.shape, b.nd, b.shape, &ond, os)) return Tensor();
    Tensor r = t_alloc(ond, os);
    int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
    for (int o = 0; o < r.size; o++) {
        int t = o;
        for (int d = ond - 1; d >= 0; d--) { out_idx[d] = t % os[d]; t /= os[d]; }
        map_coord(out_idx, ond, a.shape, a.nd, ia);
        map_coord(out_idx, ond, b.shape, b.nd, ib);
        fix bv = b.data[coord_offset(b.nd,b.shape,ib)];
        r.data[o] = (bv == 0) ? 0 : fx::fx_div(a.data[coord_offset(a.nd,a.shape,ia)], bv);
    }
    if (a.req_grad || b.req_grad) {
        requires_grad(r);
        DivNode* n = new DivNode();
        n->a = (Tensor*)&a; n->b = (Tensor*)&b; n->go = r.grad; n->out_nd = ond;
        for (int i = 0; i < ond; i++) n->out_shape[i] = os[i];
        r.fn = n; tape_push(n);
    }
    return r;
}

struct AbsNode : FnNode {
    Tensor* in; fix* go; int n;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) {
            fix s = (in->data[i] < 0) ? (fix)-fx::FX_ONE : fx::FX_ONE;
            in->grad[i] += fx::fx_mul(go[i], s);
        }
    }
    const char* name() const override { return "abs"; }
};
Tensor t_abs(const Tensor& a) {
    Tensor r = t_alloc(a.nd, a.shape);
    for (int i = 0; i < a.size; i++) r.data[i] = (a.data[i] < 0) ? (fix)-a.data[i] : a.data[i];
    if (a.req_grad) {
        requires_grad(r);
        AbsNode* n = new AbsNode();
        n->in = (Tensor*)&a; n->go = r.grad; n->n = a.size;
        r.fn = n; tape_push(n);
    }
    return r;
}

struct ExpNode : FnNode {
    Tensor* in; fix* go; fix* rdata; int n;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++)
            in->grad[i] += fx::fx_mul(rdata[i], go[i]);
    }
    const char* name() const override { return "exp"; }
};
Tensor t_exp(const Tensor& a) {
    Tensor r = t_alloc(a.nd, a.shape);
    for (int i = 0; i < a.size; i++) r.data[i] = fx::fx_exp(a.data[i]);
    if (a.req_grad) {
        requires_grad(r);
        ExpNode* n = new ExpNode();
        n->in = (Tensor*)&a; n->go = r.grad; n->n = a.size; n->rdata = r.data;
        r.fn = n; tape_push(n);
    }
    return r;
}

struct LogNode : FnNode {
    Tensor* in; fix* go; int n;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) {
            fix d = (in->data[i] == 0) ? fx::FX_ONE : in->data[i];
            in->grad[i] += fx::fx_div(go[i], d);
        }
    }
    const char* name() const override { return "log"; }
};
Tensor t_log(const Tensor& a) {
    Tensor r = t_alloc(a.nd, a.shape);
    for (int i = 0; i < a.size; i++) r.data[i] = fx::fx_ln(a.data[i] > 0 ? a.data[i] : 1);
    if (a.req_grad) {
        requires_grad(r);
        LogNode* n = new LogNode();
        n->in = (Tensor*)&a; n->go = r.grad; n->n = a.size;
        r.fn = n; tape_push(n);
    }
    return r;
}

struct ClipNode : FnNode {
    Tensor* in; fix* go; int n; fix lo, hi;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++)
            if (in->data[i] > lo && in->data[i] < hi) in->grad[i] += go[i];
    }
    const char* name() const override { return "clip"; }
};
Tensor t_clip(const Tensor& a, fix lo, fix hi) {
    Tensor r = t_alloc(a.nd, a.shape);
    for (int i = 0; i < a.size; i++) {
        fix v = a.data[i];
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        r.data[i] = v;
    }
    if (a.req_grad) {
        requires_grad(r);
        ClipNode* n = new ClipNode();
        n->in = (Tensor*)&a; n->go = r.grad; n->n = a.size; n->lo = lo; n->hi = hi;
        r.fn = n; tape_push(n);
    }
    return r;
}

struct MaximumNode : FnNode {
    Tensor* a; Tensor* b; fix* go;
    int out_nd; int out_shape[Tensor::MAX_DIM];
    void apply() override {
        int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
        for (int o = 0; o < t_numel(out_nd, out_shape); o++) {
            int t = o;
            for (int d = out_nd - 1; d >= 0; d--) { out_idx[d] = t % out_shape[d]; t /= out_shape[d]; }
            map_coord(out_idx, out_nd, a->shape, a->nd, ia);
            map_coord(out_idx, out_nd, b->shape, b->nd, ib);
            int ao = coord_offset(a->nd,a->shape,ia), bo = coord_offset(b->nd,b->shape,ib);
            if (a->grad && a->data[ao] >= b->data[bo]) a->grad[ao] += go[o];
            if (b->grad && b->data[bo] > a->data[ao])  b->grad[bo] += go[o];
        }
    }
    const char* name() const override { return "maximum"; }
};
Tensor t_maximum(const Tensor& a, const Tensor& b) {
    int os[Tensor::MAX_DIM], ond;
    if (!broadcast_shape(a.nd, a.shape, b.nd, b.shape, &ond, os)) return Tensor();
    Tensor r = t_alloc(ond, os);
    int out_idx[Tensor::MAX_DIM], ia[Tensor::MAX_DIM], ib[Tensor::MAX_DIM];
    for (int o = 0; o < r.size; o++) {
        int t = o;
        for (int d = ond - 1; d >= 0; d--) { out_idx[d] = t % os[d]; t /= os[d]; }
        map_coord(out_idx, ond, a.shape, a.nd, ia);
        map_coord(out_idx, ond, b.shape, b.nd, ib);
        fix av = a.data[coord_offset(a.nd,a.shape,ia)];
        fix bv = b.data[coord_offset(b.nd,b.shape,ib)];
        r.data[o] = (av >= bv) ? av : bv;
    }
    if (a.req_grad || b.req_grad) {
        requires_grad(r);
        MaximumNode* n = new MaximumNode();
        n->a = (Tensor*)&a; n->b = (Tensor*)&b; n->go = r.grad; n->out_nd = ond;
        for (int i = 0; i < ond; i++) n->out_shape[i] = os[i];
        r.fn = n; tape_push(n);
    }
    return r;
}

Tensor t_mean_all(const Tensor& a) {
    Tensor s = t_sum_all(a);
    fix invn = fx::fx_div(fx::FX_ONE, fx::itofix(a.size));
    return t_scalar(s, invn);
}


// ==================== 形状/切片扩展（追加） ====================
Tensor t_concat(const Tensor& a, const Tensor& b, int dim) {
    // 简化：仅 2D，dim=0 或 1
    int M=a.shape[0], N=a.shape[1];
    int Mb=b.shape[0], Nb=b.shape[1];
    if (dim==0) {
        int ns[2]={M+Mb, N};
        Tensor r = t_alloc(2, ns);
        for (int i=0;i<M*N;i++) r.data[i]=a.data[i];
        for (int i=0;i<Mb*Nb;i++) r.data[M*N+i]=b.data[i];
        return r;
    } else {
        int ns[2]={M, N+Nb};
        Tensor r = t_alloc(2, ns);
        for (int i=0;i<M;i++)
            for (int j=0;j<N;j++) r.data[i*(N+Nb)+j]=a.data[i*N+j];
        for (int i=0;i<Mb;i++)
            for (int j=0;j<Nb;j++) r.data[i*(N+Nb)+N+j]=b.data[i*Nb+j];
        return r;
    }
}

Tensor t_slice_row(const Tensor& a, int row) {
    int K = a.shape[1];
    int ns[2]={1, K};
    Tensor r = t_alloc(2, ns);
    for (int j=0;j<K;j++) r.data[j]=a.data[row*K+j];
    return r;
}

void t_argmax_row(const Tensor& a, int* out) {
    int M=a.shape[0], K=a.shape[1];
    for (int i=0;i<M;i++) {
        int best=0; fix bv=a.data[i*K];
        for (int j=1;j<K;j++) {
            if (a.data[i*K+j]>bv) { bv=a.data[i*K+j]; best=j; }
        }
        out[i]=best;
    }
}

Tensor t_max(const Tensor& a, int dim) {
    // 简化：2D，沿 dim=0 求每列最大 -> [1,N]
    int M=a.shape[0], N=a.shape[1];
    int ns[2]={1, N};
    Tensor r = t_alloc(2, ns);
    for (int j=0;j<N;j++) {
        fix mx=a.data[j];
        for (int i=1;i<M;i++) if (a.data[i*N+j]>mx) mx=a.data[i*N+j];
        r.data[j]=mx;
    }
    return r;
}

// ---------------- sigmoid / tanh / softmax_row（带反向） ----------------
struct SigmoidNode : FnNode {
    Tensor* in; fix* go; fix* rdata; int n;
    void apply() override {
        if (!in->grad) return;
        for (int i=0;i<n;i++) {
            fix s = rdata[i];
            fix ds = fx::fx_mul(s, fx::FX_ONE - s);
            in->grad[i] += fx::fx_mul(ds, go[i]);
        }
    }
    const char* name() const override { return "sigmoid"; }
};
Tensor t_sigmoid(const Tensor& a) {
    Tensor r = t_alloc(a.nd, a.shape);
    for (int i=0;i<a.size;i++) {
        fix e = fx::fx_exp(a.data[i]);
        r.data[i] = fx::fx_div(e, fx::FX_ONE + e);
    }
    if (a.req_grad) {
        requires_grad(r);
        SigmoidNode* n = new SigmoidNode();
        n->in=(Tensor*)&a; n->go=r.grad; n->n=a.size; n->rdata=r.data;
        r.fn=n; tape_push(n);
    }
    return r;
}

struct TanhNode : FnNode {
    Tensor* in; fix* go; fix* rdata; int n;
    void apply() override {
        if (!in->grad) return;
        for (int i=0;i<n;i++) {
            fix t = rdata[i];
            fix dt = fx::FX_ONE - fx::fx_mul(t,t);
            in->grad[i] += fx::fx_mul(dt, go[i]);
        }
    }
    const char* name() const override { return "tanh"; }
};
Tensor t_tanh(const Tensor& a) {
    Tensor r = t_alloc(a.nd, a.shape);
    for (int i=0;i<a.size;i++) { fix e=fx::fx_exp(fx::fx_mul(a.data[i],fx::itofix(2))); r.data[i]=fx::fx_div(e-fx::FX_ONE, e+fx::FX_ONE); }
    if (a.req_grad) {
        requires_grad(r);
        TanhNode* n = new TanhNode();
        n->in=(Tensor*)&a; n->go=r.grad; n->n=a.size; n->rdata=r.data;
        r.fn=n; tape_push(n);
    }
    return r;
}

struct SoftmaxRowNode : FnNode {
    Tensor* in; fix* go; fix* rdata; int N, C;
    void apply() override {
        if (!in->grad) return;
        for (int i=0;i<N;i++) {
            fix64 sdot = 0;
            for (int c=0;c<C;c++)
                sdot += (fix64)fx::fx_mul(go[i*C+c], rdata[i*C+c]);
            fix s = (fix)(sdot >> 16);
            for (int c=0;c<C;c++) {
                fix diff = go[i*C+c] - s;
                in->grad[i*C+c] += fx::fx_mul(rdata[i*C+c], diff);
            }
        }
    }
    const char* name() const override { return "softmax_row"; }
};
Tensor t_softmax_row(const Tensor& a) {
    int N=a.shape[0], C=a.shape[1];
    Tensor r = t_alloc(2, a.shape);
    for (int i=0;i<N;i++) {
        fix mx = a.data[i*C];
        for (int c=1;c<C;c++) if (a.data[i*C+c]>mx) mx=a.data[i*C+c];
        fix64 s=0;
        for (int c=0;c<C;c++) {
            fix e = fx::fx_exp(a.data[i*C+c]-mx);
            r.data[i*C+c]=e;
            s += (fix64)e;
        }
        fix den=(fix)s;
        for (int c=0;c<C;c++) r.data[i*C+c]=fx::fx_div(r.data[i*C+c], den);
    }
    if (a.req_grad) {
        requires_grad(r);
        SoftmaxRowNode* n = new SoftmaxRowNode();
        n->in=(Tensor*)&a; n->go=r.grad; n->rdata=r.data; n->N=N; n->C=C;
        r.fn=n; tape_push(n);
    }
    return r;
}
} // namespace deeplearn
} // namespace nefu
