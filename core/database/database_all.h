// nefuOS 嵌入式数据库 —— 聚合头
//
// 一次性引入整个数据库子系统:
//   kvstore  键值存储(哈希表 + WAL)
//   btree    B+ 树索引
//   wal      预写日志
//   sqlparse 迷你 SQL 解析器
//   sqlexec  SQL 执行引擎
//   cursor   游标 / 迭代器
//
// 用法:#include "database/database_all.h"
#pragma once

#include "wal.h"
#include "btree.h"
#include "kvstore.h"
#include "sqlparse.h"
#include "sqlexec.h"
#include "cursor.h"

namespace nefu {
namespace database {

// 跑全部子模块自检,返回失败断言总数(0 = 全部通过)。
// 逐个调用各模块的 xxx_self_test() 并汇总。
inline int database_self_test() {
    int total = 0;
    total += wal_self_test();
    total += btree_self_test();
    total += kvstore_self_test();
    total += sqlparse_self_test();
    total += sqlexec_self_test();
    total += cursor_self_test();
    return total;
}

} // namespace database
} // namespace nefu
