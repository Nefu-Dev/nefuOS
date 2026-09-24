// nefuOS 深度学习库 —— 评估指标实现
// 准确率/精确率/召回率/F1/MAE，全定点。
// 内存：new[]/delete[]，禁 STL；定点 Q16.16；无异常/RTTI。
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "metrics.h"
#include "activations.h"

namespace nefu {
namespace deeplearn {

fix metric_accuracy_binary(const Tensor& pred, const uint8_t* labels, int N) {
    int correct = 0;
    for (int i = 0; i < N; i++) {
        fix p = pred.data[i];
        // sigmoid(p) > 0.5 等价于 p > 0
        bool pos = p > 0;
        if ((pos && labels[i]) || (!pos && !labels[i])) correct++;
    }
    return fx::fx_div(fx::itofix(correct), fx::itofix(N));
}

fix metric_accuracy(const Tensor& logits, const int* labels, int N) {
    int C = logits.size / N;
    int correct = 0;
    for (int i = 0; i < N; i++) {
        int best = 0; fix bv = logits.data[i*C];
        for (int j = 1; j < C; j++)
            if (logits.data[i*C+j] > bv) { bv = logits.data[i*C+j]; best = j; }
        if (best == labels[i]) correct++;
    }
    return fx::fx_div(fx::itofix(correct), fx::itofix(N));
}

fix metric_mae(const Tensor& pred, const fix* labels, int N) {
    fix64 s = 0;
    for (int i = 0; i < N; i++) {
        fix d = pred.data[i] - labels[i];
        if (d < 0) d = -d;
        s += (fix64)d;
    }
    return fx::fx_div((fix)s, fx::itofix(N));
}

void metric_confusion(const Tensor& pred, const uint8_t* labels, int N,
                      int* tp, int* fp, int* tn, int* fn) {
    *tp=*fp=*tn=*fn=0;
    for (int i = 0; i < N; i++) {
        bool pos = pred.data[i] > 0;
        if (pos && labels[i]) (*tp)++;
        else if (pos && !labels[i]) (*fp)++;
        else if (!pos && !labels[i]) (*tn)++;
        else (*fn)++;
    }
}

fix metric_f1(int tp, int fp, int fn) {
    fix p = (tp+fp)==0 ? 0 : fx::fx_div(fx::itofix(tp), fx::itofix(tp+fp));
    fix r = (tp+fn)==0 ? 0 : fx::fx_div(fx::itofix(tp), fx::itofix(tp+fn));
    fix pr = fx::fx_mul(p, r);
    fix sum = p + r;
    if (sum == 0) return 0;
    return fx::fx_div(fx::fx_mul(fx::itofix(2), pr), sum);
}

// ---------------- 自检 ----------------
int metrics_self_test() {
    int fails = 0;
    fix tol = fx::fxf(5,100);
    // 二分类准确率：全对
    {
        fix p[4] = {fx::FX_ONE, fx::FX_ONE, -fx::FX_ONE, -fx::FX_ONE};
        uint8_t l[4] = {1,1,0,0};
        Tensor pr = t_from_flat(1,(int[1]){4},p);
        fix acc = metric_accuracy_binary(pr, l, 4);
        if (!fx_close(acc, fx::FX_ONE, tol)) fails++;
    }
    // 多分类准确率
    {
        fix lg[6] = {fx::itofix(5),0,0, 0,fx::itofix(5),0};
        int lb[2] = {0,1};
        Tensor L = t_from_flat(2,(int[2]){2,3},lg);
        fix acc = metric_accuracy(L, lb, 2);
        if (!fx_close(acc, fx::FX_ONE, tol)) fails++;
    }
    // 混淆矩阵 + F1
    {
        fix p[4] = {fx::FX_ONE, -fx::FX_ONE, fx::FX_ONE, -fx::FX_ONE};
        uint8_t l[4] = {1,0,0,1};
        Tensor pr = t_from_flat(1,(int[1]){4},p);
        int tp,fp,tn,fn;
        metric_confusion(pr, l, 4, &tp,&fp,&tn,&fn);
        // 样本: (pos,pos)=TP, (neg,neg)=TN, (pos,neg)=FP, (neg,pos)=FN
        if (tp!=1 || fp!=1 || tn!=1 || fn!=1) fails++;
        fix f1 = metric_f1(tp,fp,fn);
        // tp=1,fp=1,fn=1 -> P=0.5,R=0.5 -> F1=0.5
        if (!fx_close(f1, fx::FX_HALF, tol)) fails++;
    }
    // MAE：pred=0, labels=[1,1] -> 1
    {
        fix p[2]={0,0};
        Tensor pr=t_from_flat(1,(int[1]){2},p);
        fix l[2]={fx::FX_ONE,fx::FX_ONE};
        fix m=metric_mae(pr,l,2);
        if (!fx_close(m, fx::FX_ONE, fx::fxf(5,100))) fails++;
    }
    // F1：tp=2,fp=0,fn=0 -> 1
    {
        fix f1=metric_f1(2,0,0);
        if (!fx_close(f1, fx::FX_ONE, fx::fxf(5,100))) fails++;
    }
    return fails;
}

} // namespace deeplearn
} // namespace nefu
