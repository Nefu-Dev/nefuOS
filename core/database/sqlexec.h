// nefuOS 嵌入式数据库 —— SQL 执行引擎
//
// 职责:
//   - 表存储:行式存储,固定 schema(建表时声明列)。
//   - 执行由 sqlparse 产出的 AST:CREATE / INSERT / SELECT / DELETE / UPDATE / DROP。
//   - WHERE 条件求值:支持 = != < > <= >= 与 AND / OR;两操作数都像数字时按数值比较,
//     否则按字典序比较。
//   - ORDER BY 排序、LIMIT 截断。
//   - 简单索引:在表第一列上维护一棵 B+ 树,加速等值点查。
//   - 简化事务:BEGIN / COMMIT / ROLLBACK(BEGIN 时快照整库,ROLLBACK 整体回滚)。
#pragma once
#include "../klib/klib.h"
#include "sqlparse.h"
#include "btree.h"

namespace nefu {
namespace database {

// ---- 一行数据:按列顺序存放的文本值 ----
struct SqlRow {
    List<String> cells;
};

// ---- 一张表 ----
struct SqlTable {
    String       name;
    List<String> cols;          // 列名
    List<SqlRow> rows;          // 行数据
    BPlusTree    index;         // 在第一列上的索引(值 -> 行号)
    bool         has_index;     // 是否启用索引
};

// ---- 执行结果 ----
struct SqlResult {
    bool          ok = true;
    String        error;                  // ok=false 时的错误信息
    String        message;                // 如 "1 row inserted"
    int           affected = 0;            // 影响行数
    List<String>  col_names;              // SELECT 结果列名
    List<SqlRow>  rows;                   // SELECT 结果行
};

// ---- 数据库 ----
class Database {
public:
    Database();
    ~Database();

    // 执行一条 SQL 语句,返回结果(结果自带内存管理)。
    SqlResult exec(const char* sql);

    // 表数量 / 取表(调试用)
    int table_count() const { return tables_.size(); }
    SqlTable* find_table(const String& name);

    // 事务
    void begin_tx();
    void commit_tx();
    void rollback_tx();
    bool in_tx() const { return tx_active_; }

    // 调试:打印所有表名
    void list_tables(List<String>& out) const;

private:
    // 语句分派
    SqlResult do_create(Stmt& s);
    SqlResult do_insert(Stmt& s);
    SqlResult do_select(Stmt& s);
    SqlResult do_delete(Stmt& s);
    SqlResult do_update(Stmt& s);
    SqlResult do_drop(Stmt& s);

    // 求值:某行是否满足 WHERE
    bool row_matches(SqlTable* t, int row_idx, const WhereExpr& w);
    bool cmp_values(const String& lhs, CmpOp op, const String& rhs);

    // 维护索引
    void index_insert(SqlTable* t, int row_idx);
    void index_rebuild(SqlTable* t);

    List<SqlTable*> tables_;
    bool             tx_active_ = false;
    // 事务快照:BEGIN 时深拷贝整张表集合
    List<SqlTable*>  snapshot_;
};

// 模块自检:返回失败断言数(0 表示全部通过)
int sqlexec_self_test();

} // namespace database
} // namespace nefu
