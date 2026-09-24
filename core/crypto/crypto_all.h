// nefuOS 密码学库 —— 聚合头
//   #include "crypto/crypto_all.h"
// 暴露 nefu::crypto 下所有子模块，并汇总 crypto_self_test()。
#pragma once

#include "cipher.h"
#include "hashx.h"
#include "mac.h"
#include "kdf.h"
#include "rng.h"
#include "codec.h"
#include "classic.h"
#include "pubkey.h"

namespace nefu { namespace crypto {

// 汇总运行所有子模块的 self_test，返回总失败数（0 全部通过）
inline int crypto_self_test() {
    int f = 0;
    f += cipher_self_test();
    f += hashx_self_test();
    f += mac_self_test();
    f += kdf_self_test();
    f += rng_self_test();
    f += codec_self_test();
    f += classic_self_test();
    f += pubkey_self_test();
    return f;
}

}} // namespace
