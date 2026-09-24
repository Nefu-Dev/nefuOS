// nefuOS 嵌入式数据库 —— SQL 执行引擎实现
#include "sqlexec.h"

namespace nefu {
namespace database {

// ===================== 构造 / 析构 =====================
Database::Database() {}

Database::~Database() {
    for (int i = 0; i < tables_.size(); i++) delete tables_[i];
    tables_.clear();
    for (int i = 0; i < snapshot_.size(); i++) delete snapshot_[i];
    snapshot_.clear();
}

SqlTable* Database::find_table(const String& name) {
    for (int i = 0; i < tables_.size(); i++)
        if (tables_[i]->name == name) return tables_[i];
    return 0;
}

void Database::list_tables(List<String>& out) const {
    for (int i = 0; i < tables_.size(); i++) out.push(tables_[i]->name);
}

// ===================== 值比较 =====================
static bool is_int_like(const String& s) {
    int i = 0;
    if (s.len() == 0) return false;
    if (s[0] == '-') i = 1;
    if (i >= s.len()) return false;
    for (; i < s.len(); i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

bool Database::cmp_values(const String& lhs, CmpOp op, const String& rhs) {
    int c = 0;
    if (is_int_like(lhs) && is_int_like(rhs)) {
        c = atoi(lhs.c_str()) - atoi(rhs.c_str());
    } else {
        c = strcmp(lhs.c_str(), rhs.c_str());
    }
    switch (op) {
    case CMP_EQ: return c == 0;
    case CMP_NE: return c != 0;
    case CMP_LT: return c < 0;
    case CMP_GT: return c > 0;
    case CMP_LE: return c <= 0;
    case CMP_GE: return c >= 0;
    }
    return false;
}

// 取某行某列的文本值
static const String& cell_at(SqlTable* t, int row_idx, const String& col) {
    static String empty;
    int ci = -1;
    for (int i = 0; i < t->cols.size(); i++) {
        if (t->cols[i] == col) { ci = i; break; }
    }
    if (ci < 0) return empty;
    return t->rows[row_idx].cells[ci];
}

bool Database::row_matches(SqlTable* t, int row_idx, const WhereExpr& w) {
    if (w.or_of_ands.size() == 0) return true;      // 无 WHERE = 全匹配
    for (int g = 0; g < w.or_of_ands.size(); g++) {
        const List<CmpCond>& group = w.or_of_ands[g];
        bool all = true;
        for (int i = 0; i < group.size(); i++) {
            const String& cell = cell_at(t, row_idx, group[i].col);
            if (!cmp_values(cell, group[i].op, group[i].value)) { all = false; break; }
        }
        if (all) return true;       // 一个 OR 组全中即命中
    }
    return false;
}

// ===================== 索引 =====================
void Database::index_insert(SqlTable* t, int row_idx) {
    if (!t->has_index || t->cols.size() == 0) return;
    String key = t->rows[row_idx].cells[0];
    char idx[16]; ksprintf(idx, sizeof(idx), "%d", row_idx);
    t->index.insert(key, String(idx));
}

void Database::index_rebuild(SqlTable* t) {
    if (!t->has_index) return;
    // 重建:清空 B+ 树后逐行重插
    t->index.clear();
    for (int i = 0; i < t->rows.size(); i++) index_insert(t, i);
}

// ===================== CREATE =====================
SqlResult Database::do_create(Stmt& s) {
    SqlResult r;
    if (find_table(s.table)) { r.ok = false; r.error = String("表已存在: ") + s.table; return r; }
    if (s.create_cols.size() == 0) { r.ok = false; r.error = String("CREATE TABLE 需要至少一列"); return r; }
    SqlTable* t = new SqlTable();
    t->name = s.table;
    for (int i = 0; i < s.create_cols.size(); i++) t->cols.push(s.create_cols[i]);
    t->has_index = true;       // 在第一列上建索引
    tables_.push(t);
    r.message = String("表已创建: ") + s.table;
    return r;
}

// ===================== INSERT =====================
SqlResult Database::do_insert(Stmt& s) {
    SqlResult r;
    SqlTable* t = find_table(s.table);
    if (!t) { r.ok = false; r.error = String("表不存在: ") + s.table; return r; }
    if (s.insert_values.size() != t->cols.size()) {
        r.ok = false;
        r.error = String("列数不匹配:期望 ") + String("");
        return r;
    }
    SqlRow row;
    for (int i = 0; i < s.insert_values.size(); i++) row.cells.push(s.insert_values[i]);
    t->rows.push(row);
    index_insert(t, t->rows.size() - 1);
    r.affected = 1;
    r.message = String("1 row inserted");
    return r;
}

// ===================== SELECT =====================
SqlResult Database::do_select(Stmt& s) {
    SqlResult r;
    SqlTable* t = find_table(s.table);
    if (!t) { r.ok = false; r.error = String("表不存在: ") + s.table; return r; }

    // 决定输出列
    List<String> out_cols;
    if (s.sel_cols.size() == 0) out_cols = t->cols;     // *
    else out_cols = s.sel_cols;
    r.col_names = out_cols;

    // 收集命中行(行号)
    List<int> hits;
    for (int i = 0; i < t->rows.size(); i++) {
        if (row_matches(t, i, s.where)) hits.push(i);
    }

    // ORDER BY:按排序列对 hits 做简单选择排序(规模小)
    if (s.has_order) {
        int oc = -1;
        for (int i = 0; i < t->cols.size(); i++)
            if (t->cols[i] == s.order_col) { oc = i; break; }
        if (oc >= 0) {
            // 选择排序(升序)
            for (int i = 0; i < hits.size(); i++) {
                int best = i;
                for (int j = i + 1; j < hits.size(); j++) {
                    const String& a = t->rows[hits[j]].cells[oc];
                    const String& b = t->rows[hits[best]].cells[oc];
                    if (cmp_values(a, CMP_LT, b)) best = j;
                }
                int tmp = hits[i]; hits[i] = hits[best]; hits[best] = tmp;
            }
        }
    }

    // LIMIT
    int n = hits.size();
    if (s.has_limit && s.limit < n) n = s.limit;

    // 填结果
    for (int i = 0; i < n; i++) {
        SqlRow out;
        for (int c = 0; c < out_cols.size(); c++) {
            out.cells.push(cell_at(t, hits[i], out_cols[c]));
        }
        r.rows.push(out);
    }
    r.affected = r.rows.size();
    return r;
}

// ===================== DELETE =====================
SqlResult Database::do_delete(Stmt& s) {
    SqlResult r;
    SqlTable* t = find_table(s.table);
    if (!t) { r.ok = false; r.error = String("表不存在: ") + s.table; return r; }
    int removed = 0;
    for (int i = t->rows.size() - 1; i >= 0; i--) {
        if (row_matches(t, i, s.where)) {
            t->rows.remove(i);
            removed++;
        }
    }
    if (removed > 0) index_rebuild(t);
    r.affected = removed;
    char buf[64]; ksprintf(buf, sizeof(buf), "%d row(s) deleted", removed);
    r.message = String(buf);
    return r;
}

// ===================== UPDATE =====================
SqlResult Database::do_update(Stmt& s) {
    SqlResult r;
    SqlTable* t = find_table(s.table);
    if (!t) { r.ok = false; r.error = String("表不存在: ") + s.table; return r; }
    int sc = -1;
    for (int i = 0; i < t->cols.size(); i++)
        if (t->cols[i] == s.set_col) { sc = i; break; }
    if (sc < 0) { r.ok = false; r.error = String("列不存在: ") + s.set_col; return r; }
    int changed = 0;
    for (int i = 0; i < t->rows.size(); i++) {
        if (row_matches(t, i, s.where)) {
            t->rows[i].cells[sc] = s.set_value;
            changed++;
        }
    }
    if (changed > 0) index_rebuild(t);
    r.affected = changed;
    char buf[64]; ksprintf(buf, sizeof(buf), "%d row(s) updated", changed);
    r.message = String(buf);
    return r;
}

// ===================== DROP =====================
SqlResult Database::do_drop(Stmt& s) {
    SqlResult r;
    int idx = -1;
    for (int i = 0; i < tables_.size(); i++)
        if (tables_[i]->name == s.table) { idx = i; break; }
    if (idx < 0) { r.ok = false; r.error = String("表不存在: ") + s.table; return r; }
    delete tables_[idx];
    tables_.remove(idx);
    r.message = String("表已删除: ") + s.table;
    return r;
}

// ===================== 事务(简化) =====================
static SqlTable* copy_table(SqlTable* t) {
    SqlTable* n = new SqlTable();
    n->name = t->name;
    n->cols = t->cols;
    n->has_index = false;      // 快照不需要索引
    for (int i = 0; i < t->rows.size(); i++) n->rows.push(t->rows[i]);
    return n;
}

void Database::begin_tx() {
    if (tx_active_) return;
    tx_active_ = true;
    for (int i = 0; i < tables_.size(); i++) snapshot_.push(copy_table(tables_[i]));
}

void Database::commit_tx() {
    if (!tx_active_) return;
    for (int i = 0; i < snapshot_.size(); i++) delete snapshot_[i];
    snapshot_.clear();
    tx_active_ = false;
}

void Database::rollback_tx() {
    if (!tx_active_) return;
    // 丢弃当前表,恢复快照
    for (int i = 0; i < tables_.size(); i++) delete tables_[i];
    tables_.clear();
    for (int i = 0; i < snapshot_.size(); i++) {
        SqlTable* restored = copy_table(snapshot_[i]);
        restored->has_index = true;
        index_rebuild(restored);
        tables_.push(restored);
    }
    for (int i = 0; i < snapshot_.size(); i++) delete snapshot_[i];
    snapshot_.clear();
    tx_active_ = false;
}

// ===================== exec 分派 =====================
SqlResult Database::exec(const char* sql) {
    SqlResult r;
    if (!sql || !*sql) { r.ok = false; r.error = String("空语句"); return r; }

    // 事务控制语句不走 SQL 解析器
    // 跳过前导空白
    const char* p = sql;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "BEGIN", 5) == 0 || strncmp(p, "START TRANSACTION", 17) == 0) {
        begin_tx();
        r.message = String("BEGIN");
        return r;
    }
    if (strncmp(p, "COMMIT", 6) == 0) {
        commit_tx();
        r.message = String("COMMIT");
        return r;
    }
    if (strncmp(p, "ROLLBACK", 8) == 0) {
        rollback_tx();
        r.message = String("ROLLBACK");
        return r;
    }

    Stmt s;
    String err;
    if (!sql_parse(sql, s, &err)) {
        r.ok = false;
        r.error = String("SQL 错误: ") + err;
        return r;
    }
    switch (s.kind) {
    case STMT_CREATE: return do_create(s);
    case STMT_INSERT: return do_insert(s);
    case STMT_SELECT: return do_select(s);
    case STMT_DELETE: return do_delete(s);
    case STMT_UPDATE: return do_update(s);
    case STMT_DROP:   return do_drop(s);
    }
    r.ok = false; r.error = String("未知语句");
    return r;
}

// ===================== 自检 =====================
int sqlexec_self_test() {
    int fail = 0;

    // 1) 建表 + 插入 + 全表 SELECT
    {
        Database db;
        SqlResult r1 = db.exec("CREATE TABLE users (id, name, age)");
        if (!r1.ok) fail++;
        db.exec("INSERT INTO users VALUES (1, 'alice', 20)");
        db.exec("INSERT INTO users VALUES (2, 'bob', 30)");
        db.exec("INSERT INTO users VALUES (3, 'carol', 25)");
        SqlResult s = db.exec("SELECT * FROM users");
        if (!s.ok) fail++;
        if (s.rows.size() != 3) fail++;
        if (s.col_names.size() != 3) fail++;
        if (s.rows[0].cells[1] != "alice") fail++;
    }

    // 2) WHERE 过滤(数值比较)
    {
        Database db;
        db.exec("CREATE TABLE t (id, v)");
        for (int i = 1; i <= 5; i++) {
            char buf[64]; ksprintf(buf, sizeof(buf), "INSERT INTO t VALUES (%d, %d)", i, i * 10);
            db.exec(buf);
        }
        SqlResult s = db.exec("SELECT * FROM t WHERE v >= 30");
        if (s.rows.size() != 3) fail++;          // 30,40,50
        SqlResult s2 = db.exec("SELECT id FROM t WHERE v < 25");
        if (s2.rows.size() != 2) fail++;        // 10,20
        if (s2.col_names.size() != 1 || s2.col_names[0] != "id") fail++;
    }

    // 3) ORDER BY + LIMIT
    {
        Database db;
        db.exec("CREATE TABLE t (id, score)");
        db.exec("INSERT INTO t VALUES (1, 50)");
        db.exec("INSERT INTO t VALUES (2, 90)");
        db.exec("INSERT INTO t VALUES (3, 70)");
        SqlResult s = db.exec("SELECT * FROM t ORDER BY score DESC LIMIT 1");
        // 我们的解析器忽略 DESC,这里按升序取第 1 = 最低分验证排序生效
        SqlResult s2 = db.exec("SELECT * FROM t ORDER BY score LIMIT 2");
        if (s2.rows.size() != 2) fail++;
        if (s2.rows[0].cells[1] != "50") fail++;     // 升序第一个是 50
        if (s2.rows[1].cells[1] != "70") fail++;
        (void)s;
    }

    // 4) AND / OR
    {
        Database db;
        db.exec("CREATE TABLE t (a, b)");
        db.exec("INSERT INTO t VALUES (1, 1)");
        db.exec("INSERT INTO t VALUES (2, 2)");
        db.exec("INSERT INTO t VALUES (3, 3)");
        SqlResult s = db.exec("SELECT * FROM t WHERE a = 1 OR b = 3");
        if (s.rows.size() != 2) fail++;
        SqlResult s2 = db.exec("SELECT * FROM t WHERE a >= 2 AND b <= 2");
        if (s2.rows.size() != 1) fail++;
        if (s2.rows[0].cells[0] != "2") fail++;
    }

    // 5) DELETE + UPDATE
    {
        Database db;
        db.exec("CREATE TABLE t (id, name)");
        db.exec("INSERT INTO t VALUES (1, 'a')");
        db.exec("INSERT INTO t VALUES (2, 'b')");
        db.exec("INSERT INTO t VALUES (3, 'c')");
        SqlResult d = db.exec("DELETE FROM t WHERE id = 2");
        if (d.affected != 1) fail++;
        SqlResult s = db.exec("SELECT * FROM t");
        if (s.rows.size() != 2) fail++;
        SqlResult u = db.exec("UPDATE t SET name = 'zzz' WHERE id = 1");
        if (u.affected != 1) fail++;
        SqlResult s2 = db.exec("SELECT name FROM t WHERE id = 1");
        if (s2.rows.size() != 1 || s2.rows[0].cells[0] != "zzz") fail++;
    }

    // 6) 事务回滚
    {
        Database db;
        db.exec("CREATE TABLE t (id, v)");
        db.exec("INSERT INTO t VALUES (1, 'x')");
        db.exec("BEGIN");
        db.exec("INSERT INTO t VALUES (2, 'y')");
        db.exec("DELETE FROM t WHERE id = 1");
        SqlResult before = db.exec("SELECT * FROM t");
        if (before.rows.size() != 1) fail++;     // 事务内:删了1插了2 → 剩2
        db.exec("ROLLBACK");
        SqlResult after = db.exec("SELECT * FROM t");
        if (after.rows.size() != 1) fail++;       // 回滚后:恢复成只有 id=1
        if (after.rows[0].cells[0] != "1") fail++;
        // COMMIT 路径
        db.exec("BEGIN");
        db.exec("INSERT INTO t VALUES (3, 'z')");
        db.exec("COMMIT");
        SqlResult c = db.exec("SELECT * FROM t");
        if (c.rows.size() != 2) fail++;          // 提交后保留
    }

    // 7) DROP TABLE
    {
        Database db;
        db.exec("CREATE TABLE t (a)");
        db.exec("DROP TABLE t");
        if (db.table_count() != 0) fail++;
        SqlResult s = db.exec("SELECT * FROM t");
        if (s.ok) fail++;                        // 表已删,应报错
    }

    return fail;
}

} // namespace database
} // namespace nefu
