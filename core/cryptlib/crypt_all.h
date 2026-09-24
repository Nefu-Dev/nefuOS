// nefuOS crypto library — 聚合头（STL 风格）
// 一次 include 全部密码学模块。自测：crypt_all_self_test() 汇总各模块失败数。
#pragma once
#include "cryptlib/sha256.h"
#include "cryptlib/sha1.h"
#include "cryptlib/md5.h"
#include "cryptlib/crc.h"
#include "cryptlib/b64.h"
#include "cryptlib/hmac.h"
#include "cryptlib/pbkdf2.h"
#include "cryptlib/aes.h"
#include "cryptlib/rc4.h"
#include "cryptlib/xor.h"

namespace nefu {
namespace crypt {

// 汇总自测：返回全部模块失败总数（0 = 全过）
inline int crypt_all_self_test() {
    return sha256_self_test() + sha1_self_test() + md5_self_test() +
           crc_self_test() + b64_self_test() + hmac_self_test() +
           pbkdf2_self_test() + aes_self_test() + rc4_self_test() +
           xor_self_test();
}

} // namespace crypt
} // namespace nefu
