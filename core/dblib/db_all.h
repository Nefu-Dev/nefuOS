// nefuOS 数据存储库 dblib —— STL 风格聚合头
// 一次 include 全部数据模块。自测：db_all_self_test()。
// 教学版：class / std 模板 / do-while / switch / cmath，中文注释。
#pragma once

#include "dblib/csv.h"
#include "dblib/ini.h"
#include "dblib/jsonstore.h"
#include "dblib/bptree.h"
#include "dblib/page.h"
#include "dblib/table.h"

namespace nefu {
namespace dbx {

// 汇总自测：返回失败总数，0 表示全部通过
inline int db_all_self_test() {
    return CsvTable::self_test() + Ini::self_test() +
           Json::self_test() + BpTree::self_test() +
           PageManager::self_test() + Table::self_test();
}

} // namespace dbx
} // namespace nefu
