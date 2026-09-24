// nefuOS 嵌入式数据库 —— 游标 / 迭代器
//
// 游标是 "按行遍历表" 的统一抽象,屏蔽底层是全表扫描还是索引扫描:
//   - 全表扫描(open_full):从头到尾逐行走。
//   - 索引扫描(open_index):利用第一列上的 B+ 树直接定位某一行。
//   - 范围扫描(open_range):利用 B+ 树的叶子链表,取出 [lo, hi] 区间的行。
//
// 使用模式:
//   DbCursor c;
//   c.open_full(table);
//   while (!c.done()) {
//       SqlRow row; c.current_row(row);
//       ...
//       c.next();
//   }
//   c.close();
#pragma once
#include "../klib/klib.h"
#include "sqlexec.h"

namespace nefu {
namespace database {

class DbCursor {
public:
    DbCursor();
    ~DbCursor();

    // 三种打开方式
    void open_full(SqlTable* t);
    bool open_index(SqlTable* t, const String& key);     // 定位第一列 = key 的行
    void open_range(SqlTable* t, const String& lo, const String& hi);

    bool done() const { return done_; }

    // 后移 / 前移
    void next();
    void prev();

    // 把当前行拷到 out。done 时返回 false。
    bool current_row(SqlRow& out) const;
    // 当前行的行号(-1 表示无效)
    int  row_index() const { return row_idx_; }

    void close();

private:
    SqlTable*     t_;
    int           row_idx_;
    bool          done_;
    // 索引 / 范围扫描时,预先取出命中的行号列表
    List<int>     hit_rows_;
    int           hit_pos_;
    bool          using_hits_;
};

// 模块自检:返回失败断言数(0 表示全部通过)
int cursor_self_test();

} // namespace database
} // namespace nefu
