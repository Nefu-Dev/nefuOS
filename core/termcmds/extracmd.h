// =============================================================================
//  extracmd.h —— 扩展命令与真实算法: base64 / crc32 / bc / tr / cut / comm /
//                env / traceroute / factor
// -----------------------------------------------------------------------------
//  这些都是可独立单元测试的纯算法或半真实命令, 不依赖外部后端。
// =============================================================================
#pragma once

#include "termcmds_all.h"
#include "filecmd.h"

namespace nefu {
namespace termcmds {

// ---- base64 ----
nefu::String base64_encode(const char* data, int len);
bool         base64_decode(const char* b64, nefu::String& out);

// ---- CRC32(表驱动, IEEE) ----
uint32_t     crc32_calc(const char* data, int len);

// ---- bc 表达式求值 ----
bool         bc_eval(const char* expr, double* result);

// ---- tr: 把 set1 中的字符替换为 set2 对应位, delete_mode 时删除 ----
nefu::String tr_translate(const char* text, const char* set1, const char* set2, bool delete_mode);

// ---- cut: 按分隔符 sep 取第 field 列(1-based) ----
nefu::String cut_field(const char* line, char sep, int field);

// ---- comm: 两个已排序行列表的交集/差集 ----
//  out_both / out_only1 / out_only2 分别追加对应行。
void comm_compare(const nefu::List<nefu::String>& a,
                  const nefu::List<nefu::String>& b,
                  nefu::List<nefu::String>& only1,
                  nefu::List<nefu::String>& only2,
                  nefu::List<nefu::String>& both);

// ---- 环境变量表 ----
void env_clear();
void env_set(const char* name, const char* value);
const char* env_get(const char* name);

// ---- factor: 质因数分解, 结果追加到 factors(升序) ----
void factorize(uint32_t n, nefu::List<uint32_t>& factors);

int extracmd_self_test();

} // namespace termcmds
} // namespace nefu
