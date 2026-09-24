// nefuOS 深度学习库 —— 高阶模型包装实现
// MLP/TinyCNN 高阶包装。注意跨函数 forward 后不要直接 backward。
// 内存：new[]/delete[]，禁 STL；定点 Q16.16；无异常/RTTI。
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "models.h"
#include "autograd.h"
#include "losses.h"
#include "optimizers.h"

namespace nefu {
namespace deeplearn {

// ==================== MLP ====================
MLP::MLP(const int* sizes, int n, uint32_t seed, int hidden_act) {
    n_layers = n - 1;
    layers = new Dense*[n_layers];
    act_types = new int[n_layers];
    in_dim = sizes[0];
    for (int i = 0; i < n_layers; i++) {
        layers[i] = new Dense(sizes[i], sizes[i+1], seed + i*7);
        act_types[i] = (i == n_layers-1) ? 2 : hidden_act;  // 输出层默认无激活
    }
}
MLP::~MLP() {
    for (int i = 0; i < n_layers; i++) delete layers[i];
    delete[] layers;
    delete[] act_types;
}
Tensor MLP::forward(const Tensor& x) {
    Tensor h = t_copy(x);
    for (int i = 0; i < n_layers; i++) {
        Tensor y = layers[i]->forward(h);
        if (act_types[i] == 0)       h = act_relu(y);
        else if (act_types[i] == 1)  h = act_tanh(y);
        else                          h = (Tensor&&)y;
    }
    return h;
}
void MLP::params(List<Tensor*>& out) {
    for (int i = 0; i < n_layers; i++) layers[i]->params(out);
}

// ==================== TinyCNN ====================
TinyCNN::TinyCNN(int H, int W, int classes, uint32_t seed)
    : conv(4, 1, 3, 3, seed), in_h(H), in_w(W), classes(classes) {
    // 输出尺寸：(H-2)/2
    int fh = (H - 2) / 2;
    int fw = (W - 2) / 2;
    fc = Dense(4*fh*fw, classes, seed+1);
}
Tensor TinyCNN::forward(const Tensor& x) {
    Tensor y = conv.forward(x);
    y = act_relu(y);
    y = pool.forward(y);
    Tensor f = layer_flatten(y);
    Tensor o = fc.forward(f);
    return o;
}
void TinyCNN::params(List<Tensor*>& out) {
    conv.params(out);
    fc.params(out);
}

// ==================== XOR demo ====================
fix mlp_xor_demo(uint32_t seed, int epochs) {
    fix xor_x[4][2] = {
        {0,0},{0,fx::FX_ONE},{fx::FX_ONE,0},{fx::FX_ONE,fx::FX_ONE}
    };
    int xor_y[4] = {0,1,1,0};
    int sizes[3] = {2,4,1};
    MLP mlp(sizes, 3, seed, 0);
    List<Tensor*> ps; mlp.params(ps);
    Adam opt(fx::fxf(1,100)); opt.add_all(ps);
    fix last = 0;
    for (int ep = 0; ep < epochs; ep++) {
        tape_reset();
        Tensor X = t_from_flat(2,(int[2]){4,2},(fix*)xor_x[0]);
        Tensor y = mlp.forward(X);
        fix tv[4]; for (int i=0;i<4;i++) tv[i] = xor_y[i] ? fx::FX_ONE : 0;
        Tensor T = t_from_flat(2,(int[2]){4,1},tv);
        Tensor loss = loss_mse(y, T);
        backward(loss);
        opt.step(); opt.zero_grad();
        last = loss.data[0];
        tape_reset();
    }
    return last;
}

// ---------------- 自检 ----------------
int models_self_test() {
    int fails = 0;
    // MLP 前向形状
    {
        int sizes[3]={2,4,1};
        MLP mlp(sizes,3,42,0);
        fix xv[4][2]={{0,0},{0,fx::FX_ONE},{fx::FX_ONE,0},{fx::FX_ONE,fx::FX_ONE}};
        Tensor X=t_from_flat(2,(int[2]){4,2},(fix*)xv[0]);
        Tensor y=mlp.forward(X);
        if (y.shape[0]!=4 || y.shape[1]!=1) fails++;
    }
    // MLP 多层前向：输出非全零
    {
        int sizes[4]={3,5,5,2};
        MLP mlp(sizes,4,99,1);
        fix xv[6]={fx::FX_ONE,fx::FX_HALF,0, 0,fx::FX_ONE,fx::FX_HALF};
        Tensor X=t_from_flat(2,(int[2]){2,3},xv);
        Tensor y=mlp.forward(X);
        if (y.shape[0]!=2 || y.shape[1]!=2) fails++;
        bool any=false;
        for(int i=0;i<4;i++) if(y.data[i]!=0) any=true;
        if(!any) fails++;
    }
    // params 收集数量：两层 = W+b 共 4 个
    {
        int sizes[3]={2,4,1};
        MLP mlp(sizes,3,5,0);
        List<Tensor*> ps; mlp.params(ps);
        if (ps.size() != 4) fails++;
    }
    // TinyCNN 前向：[1,1,8,8] -> [1, classes]
    {
        TinyCNN cnn(8, 8, 3, 21);
        fix v[64]; for(int i=0;i<64;i++) v[i]=fx::itofix(i%4);
        Tensor x=t_from_flat(4,(int[4]){1,1,8,8},v);
        Tensor y=cnn.forward(x);
        if (y.shape[0]!=1 || y.shape[1]!=3) fails++;
    }
    // MLP params 数量校验（三层）
    {
        int sizes[4]={4,6,6,2};
        MLP mlp(sizes,4,33,0);
        List<Tensor*> ps; mlp.params(ps);
        if (ps.size() != 6) fails++;  // 3 层 W+b
    }
    return fails;
}

} // namespace deeplearn
} // namespace nefu
