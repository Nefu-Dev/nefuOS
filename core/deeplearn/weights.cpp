// nefuOS 深度学习库 —— 参数序列化实现
// 参数打包/解包为连续字节缓冲，小端 int32。
// 内存：new[]/delete[]，禁 STL；定点 Q16.16；无异常/RTTI。
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "weights.h"
#include "../platform.h"

namespace nefu {
namespace deeplearn {

namespace {
// 小端写入/读取（裸机与宿主同构，直接 memcpy 风格）
void wr_u32(unsigned char*& p, uint32_t v) {
    p[0]=(unsigned char)(v&0xff); p[1]=(unsigned char)((v>>8)&0xff);
    p[2]=(unsigned char)((v>>16)&0xff); p[3]=(unsigned char)((v>>24)&0xff);
    p+=4;
}
uint32_t rd_u32(const unsigned char*& p) {
    uint32_t v=(uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    p+=4; return v;
}
void wr_fix(unsigned char*& p, fix v) {
    uint32_t u=(uint32_t)v;
    wr_u32(p,u);
}
fix rd_fix(const unsigned char*& p) {
    return (fix)rd_u32(p);
}
} // namespace

size_t pack_params_size(const List<Tensor*>& params) {
    size_t sz = 4;  // count
    for (int i=0;i<params.size();i++) {
        Tensor* t = params[i];
        sz += 4;                          // nd
        sz += 4 * (size_t)t->nd;          // shape
        sz += 4;                          // size
        sz += 4 * (size_t)t->size;        // data
    }
    return sz;
}

void* pack_params(const List<Tensor*>& params, size_t* out_bytes) {
    size_t sz = pack_params_size(params);
    unsigned char* buf = (unsigned char*)kalloc(sz);
    unsigned char* p = buf;
    wr_u32(p, (uint32_t)params.size());
    for (int i=0;i<params.size();i++) {
        Tensor* t = params[i];
        wr_u32(p, (uint32_t)t->nd);
        for (int d=0;d<t->nd;d++) wr_u32(p, (uint32_t)t->shape[d]);
        wr_u32(p, (uint32_t)t->size);
        for (int j=0;j<t->size;j++) wr_fix(p, t->data[j]);
    }
    if (out_bytes) *out_bytes = sz;
    return buf;
}

size_t unpack_params(const void* buf, List<Tensor*>& params) {
    const unsigned char* p = (const unsigned char*)buf;
    uint32_t count = rd_u32(p);
    if ((int)count != params.size()) return 0;
    for (int i=0;i<(int)count;i++) {
        Tensor* t = params[i];
        uint32_t nd = rd_u32(p);
        if ((int)nd != t->nd) return 0;
        for (int d=0;d<(int)nd;d++) {
            uint32_t sd = rd_u32(p);
            if ((int)sd != t->shape[d]) return 0;
        }
        uint32_t sz = rd_u32(p);
        if ((int)sz != t->size) return 0;
        for (int j=0;j<(int)sz;j++) t->data[j] = rd_fix(p);
    }
    return (size_t)(p - (const unsigned char*)buf);
}

// ---------------- 自检 ----------------
int weights_self_test() {
    int fails = 0;
    // 打包->解包，数据一致
    {
        fix v1[4] = {fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4)};
        fix v2[2] = {fx::FX_HALF, fx::FX_ONE};
        Tensor a = t_from_flat(2,(int[2]){2,2},v1);
        Tensor b = t_from_flat(1,(int[1]){2},v2);
        List<Tensor*> ps; ps.push(&a); ps.push(&b);
        size_t bytes;
        void* buf = pack_params(ps, &bytes);
        // 改坏原值
        a.data[0] = 0; b.data[1] = 0;
        size_t rd = unpack_params(buf, ps);
        if (rd != bytes) fails++;
        if (!fx_close(a.data[0], fx::itofix(1), fx::fxf(1,100))) fails++;
        if (!fx_close(b.data[1], fx::FX_ONE, fx::fxf(1,100))) fails++;
        kfree(buf);
    }
    // 打包大小：2 个张量（2x2 + 2）
    {
        fix v1[4]={1,2,3,4};
        fix v2[2]={fx::FX_HALF,fx::FX_ONE};
        Tensor a=t_from_flat(2,(int[2]){2,2},v1);
        Tensor b=t_from_flat(1,(int[1]){2},v2);
        List<Tensor*> ps; ps.push(&a); ps.push(&b);
        size_t sz=pack_params_size(ps);
        // count(4) + nd(4)+shape(8)+size(4)+data(16) + nd(4)+shape(4)+size(4)+data(8)
        if (sz < 40) fails++;
    }
    return fails;
}

} // namespace deeplearn
} // namespace nefu
