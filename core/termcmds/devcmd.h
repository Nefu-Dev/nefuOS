// =============================================================================
//  devcmd.h —— 开发/工具命令: xxd/hexdump/strings/file/which/find/man/help/history
// =============================================================================
#pragma once

#include "termcmds_all.h"
#include "filecmd.h"

namespace nefu {
namespace termcmds {

// ---- 纯算法 ----
// 从数据中提取长度 >= min_len 的可打印字符串,追加到 out。
void strings_extract(const char* data, int len, int min_len,
                     nefu::List<nefu::String>& out);

// magic 探测: 按文件头返回类型字符串(ELF/PE/PNG/JPEG/ZIP/UTF-8 text/...)
const char* file_detect(const char* data, int len);

// 在 VFS 里递归查找名字匹配 pattern 的节点(glob),路径追加到 out。
void find_walk(VfsNode* dir, const char* prefix, const char* pattern,
               nefu::List<nefu::String>& out);

// ---- history 环形缓冲 ----
void history_clear();
void history_push(const char* cmd);
int  history_count();
const char* history_get(int i);   // 0 = oldest

// ---- base64(真实编解码) ----
nefu::String base64_encode(const char* data, int len);
bool         base64_decode(const char* b64, nefu::String& out);

// ---- CRC32(真实, 表驱动, IEEE 802.3 多项式) ----
uint32_t     crc32_calc(const char* data, int len);

// ---- bc 极简计算器: 求值一个算术表达式, 返回是否成功 ----
bool bc_eval(const char* expr, double* result);

int devcmd_self_test();

} // namespace termcmds
} // namespace nefu
