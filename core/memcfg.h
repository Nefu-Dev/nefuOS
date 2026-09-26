#pragma once
// nefuOS memory budget model — logical system layout (4 GB).
//   System total ............ 4096 MB  (SYS_MEM_TOTAL_MB)
//   Kernel + NVFS + network ..  512 MB  (bare-metal kalloc arena)
//   Browser .................. 2048 MB  (bare-metal balloc arena)
//   Reserve / other .......... 1536 MB  (unallocated headroom)
// On the host (win32) backend allocations are plain malloc/free, so these
// budgets act as reported caps + page caps rather than hard physical limits.
#include <stdint.h>

const uint64_t SYS_MEM_TOTAL_MB      = 4096;   // 系统总内存上限 4GB
const uint64_t KERNEL_MEM_BUDGET_MB  = 512;    // 内核 + NVFS + 网络
const uint64_t BROWSER_MEM_BUDGET_MB = 2048;   // 浏览器
const uint64_t RESERVE_MEM_MB        = 1536;   // 余量

// Browser-arena accounting (bytes currently handed out via balloc/bfree).
extern uint64_t browser_mem_used_bytes;
