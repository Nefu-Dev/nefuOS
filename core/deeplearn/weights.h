// nefuOS 深度学习库 —— 参数序列化
// 典型用法：
//   n = pack_params_size(params, n);
//   buf = new uint8[n]; pack_params(params, n, buf);
//   // 写 flash / 通过串口发送 ...
//   unpack_params(params, n, buf);  // 形状必须与打包时一致
//
// 格式：[u32 count][每张量: u32 nd][shape[nd]][u32 size][size*fix]
// fix 以 int32 小端原样写入。解包时形状必须完全一致。
// 用于保存/加载训练好的模型权重，裸机下可写入 flash。
// 把一组参数张量打包成连续字节缓冲（用于保存/加载模型权重）。
// 格式：[u32 count][每个张量: u32 nd][shape[nd]][u32 size][size*fix data]
// fix 以 int32 原样写入。无 STL，用 new[]。
#pragma once
#include "tensor.h"

namespace nefu {
namespace deeplearn {

// 把参数列表打包到新分配的缓冲，返回缓冲指针与字节长度（写入 out_bytes）。
// 调用方负责 kfree。
void* pack_params(const List<Tensor*>& params, size_t* out_bytes);

// 从缓冲解包到已存在的参数列表（形状必须一致）。返回读取字节数。
size_t unpack_params(const void* buf, List<Tensor*>& params);

// 计算打包所需字节数（不分配）。
size_t pack_params_size(const List<Tensor*>& params);

int weights_self_test();

} // namespace deeplearn
} // namespace nefu
