// nefuOS crypto library — RC4 流密码 (rc4)
// RC4：经典流密码（1987 年 Ron Rivest）。256 字节状态 S 经 KSA
// 密钥调度 + PRGA 伪随机生成，逐字节异或。历史用途：WEP/WPA、
// TLS 早期。已证明不安全（2001 年后多篇攻击论文），教学中仅作
// 流密码结构学习：密钥流与明文等长、加解密同一函数。
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace crypt {

// 加解密同一函数（流密码对称）：data 原地变换
void rc4(const unsigned char* key, int klen, unsigned char* data, int n);

int rc4_self_test();

} // namespace crypt
} // namespace nefu
