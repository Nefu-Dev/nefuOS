// nefuOS 嵌入式数据库 —— 迷你 SQL 解析器实现
#include "sqlparse.h"

namespace nefu {
namespace database {

// ===================== 词法分析 =====================
enum TokKind { TOK_EOF, TOK_ID, TOK_STRING, TOK_NUMBER, TOK_SYM };

struct Token {
    TokKind kind;
    String  text;
};

// 把 SQL 切成 token 流
static void lex(const char* sql, List<Token>& out) {
    const char* p = sql;
    while (*p) {
        // 跳过空白
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;

        // 字符串字面量:'...' 或 "..."
        if (*p == '\'' || *p == '"') {
            char quote = *p++;
            String s;
            while (*p && *p != quote) { s += *p; p++; }
            if (*p == quote) p++;
            Token t; t.kind = TOK_STRING; t.text = s;
            out.push(t);
            continue;
        }

        // 数字
        if ((*p >= '0' && *p <= '9')) {
            String s;
            while (*p && ((*p >= '0' && *p <= '9') || *p == '.')) { s += *p; p++; }
            Token t; t.kind = TOK_NUMBER; t.text = s;
            out.push(t);
            continue;
        }

        // 标识符 / 关键字
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            String s;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                   (*p >= '0' && *p <= '9') || *p == '_') { s += *p; p++; }
            Token t; t.kind = TOK_ID; t.text = s;
            out.push(t);
            continue;
        }

        // 单字符符号(含多字符运算符 != >= <=)
        String s;
        s += *p;
        if ((*p == '!' && *(p + 1) == '=') ||
            (*p == '>' && *(p + 1) == '=') ||
            (*p == '<' && *(p + 1) == '=') ||
            (*p == '<' && *(p + 1) == '>') ||
            (*p == '=' && *(p + 1) == '=')) {
            s += *(p + 1);
            p += 2;
        } else {
            p++;
        }
        Token t; t.kind = TOK_SYM; t.text = s;
        out.push(t);
    }
    Token eof; eof.kind = TOK_EOF; eof.text = String("");
    out.push(eof);
}

// 大写比较(忽略大小写判断关键字)
static bool kw(const String& id, const char* word) {
    // id 是标识符,word 全大写
    if (id.len() != (int)strlen(word)) return false;
    for (int i = 0; i < id.len(); i++) {
        char c = id[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c != word[i]) return false;
    }
    return true;
}

const char* cmp_op_str(CmpOp op) {
    switch (op) {
    case CMP_EQ: return "=";
    case CMP_NE: return "!=";
    case CMP_LT: return "<";
    case CMP_GT: return ">";
    case CMP_LE: return "<=";
    case CMP_GE: return ">=";
    }
    return "?";
}

// ===================== 递归下降解析器 =====================
struct Parser {
    List<Token> toks;
    int pos = 0;
    String* err;

    Token& peek() { return toks[pos]; }
    Token& next() { return toks[pos++]; }
    bool eof() { return peek().kind == TOK_EOF; }

    bool expect_sym(const char* sym) {
        if (peek().kind == TOK_SYM && peek().text == sym) { next(); return true; }
        if (err) *err = String("语法错误:期望 ") + String(sym);
        return false;
    }
    bool expect_kw(const char* word) {
        if (peek().kind == TOK_ID && kw(peek().text, word)) { next(); return true; }
        if (err) *err = String("语法错误:期望关键字 ") + String(word);
        return false;
    }
    bool accept_kw(const char* word) {
        if (peek().kind == TOK_ID && kw(peek().text, word)) { next(); return true; }
        return false;
    }

    // 读一个标识符(表名 / 列名)
    bool parse_name(String& out) {
        if (peek().kind != TOK_ID) {
            if (err) *err = String("语法错误:期望名称");
            return false;
        }
        out = next().text;
        return true;
    }

    // 读一个字面量值(字符串或数字),返回其文本
    bool parse_literal(String& out) {
        if (peek().kind == TOK_STRING || peek().kind == TOK_NUMBER) {
            out = next().text;
            return true;
        }
        if (err) *err = String("语法错误:期望字面量");
        return false;
    }

    // 比较条件:col op value
    bool parse_cmp(CmpCond& c) {
        if (!parse_name(c.col)) return false;
        String op = peek().text;
        if (peek().kind == TOK_SYM) {
            if (op == "=" || op == "==") c.op = CMP_EQ;
            else if (op == "!=" || op == "<>") c.op = CMP_NE;
            else if (op == "<") c.op = CMP_LT;
            else if (op == ">") c.op = CMP_GT;
            else if (op == "<=") c.op = CMP_LE;
            else if (op == ">=") c.op = CMP_GE;
            else { if (err) *err = String("语法错误:期望比较运算符"); return false; }
            next();
        } else {
            if (err) *err = String("语法错误:期望比较运算符");
            return false;
        }
        return parse_literal(c.value);
    }

    // 一个 AND 组:CmpCond (AND CmpCond)*
    bool parse_and_group(List<CmpCond>& group) {
        CmpCond c;
        if (!parse_cmp(c)) return false;
        group.push(c);
        while (accept_kw("AND")) {
            CmpCond c2;
            if (!parse_cmp(c2)) return false;
            group.push(c2);
        }
        return true;
    }

    // WHERE:AND 组 (OR AND 组)*
    bool parse_where(WhereExpr& w) {
        List<CmpCond> g;
        if (!parse_and_group(g)) return false;
        w.or_of_ands.push(g);
        while (accept_kw("OR")) {
            List<CmpCond> g2;
            if (!parse_and_group(g2)) return false;
            w.or_of_ands.push(g2);
        }
        return true;
    }

    bool parse_create(Stmt& s) {
        if (!expect_kw("TABLE")) return false;
        if (!parse_name(s.table)) return false;
        if (!expect_sym("(")) return false;
        // 列名列表:name (, name)*
        String col;
        if (!parse_name(col)) return false;
        s.create_cols.push(col);
        while (peek().kind == TOK_SYM && peek().text == ",") {
            next();
            if (!parse_name(col)) return false;
            s.create_cols.push(col);
        }
        if (!expect_sym(")")) return false;
        return true;
    }

    bool parse_insert(Stmt& s) {
        if (!accept_kw("INTO")) { if (err) *err = String("INSERT 后需要 INTO"); return false; }
        if (!parse_name(s.table)) return false;
        if (!expect_kw("VALUES")) return false;
        if (!expect_sym("(")) return false;
        String v;
        if (!parse_literal(v)) return false;
        s.insert_values.push(v);
        while (peek().kind == TOK_SYM && peek().text == ",") {
            next();
            if (!parse_literal(v)) return false;
            s.insert_values.push(v);
        }
        if (!expect_sym(")")) return false;
        return true;
    }

    bool parse_select(Stmt& s) {
        // 选列:* 或 name (, name)*
        if (peek().kind == TOK_SYM && peek().text == "*") {
            next();   // sel_cols 留空表示 *
        } else {
            String col;
            if (!parse_name(col)) return false;
            s.sel_cols.push(col);
            while (peek().kind == TOK_SYM && peek().text == ",") {
                next();
                if (!parse_name(col)) return false;
                s.sel_cols.push(col);
            }
        }
        if (!expect_kw("FROM")) return false;
        if (!parse_name(s.table)) return false;
        // 可选 WHERE
        if (accept_kw("WHERE")) {
            if (!parse_where(s.where)) return false;
        }
        // 可选 ORDER BY
        if (accept_kw("ORDER")) {
            if (!expect_kw("BY")) return false;
            if (!parse_name(s.order_col)) return false;
            s.has_order = true;
            // ASC / DESC 忽略(默认升序)
            accept_kw("ASC");
            accept_kw("DESC");
        }
        // 可选 LIMIT
        if (accept_kw("LIMIT")) {
            if (peek().kind != TOK_NUMBER) { if (err) *err = String("LIMIT 后需要数字"); return false; }
            s.limit = atoi(next().text.c_str());
            s.has_limit = true;
        }
        return true;
    }

    bool parse_delete(Stmt& s) {
        if (!expect_kw("FROM")) return false;
        if (!parse_name(s.table)) return false;
        if (accept_kw("WHERE")) {
            if (!parse_where(s.where)) return false;
        }
        return true;
    }

    bool parse_update(Stmt& s) {
        if (!parse_name(s.table)) return false;
        if (!expect_kw("SET")) return false;
        if (!parse_name(s.set_col)) return false;
        if (!expect_sym("=")) return false;
        if (!parse_literal(s.set_value)) return false;
        if (accept_kw("WHERE")) {
            if (!parse_where(s.where)) return false;
        }
        return true;
    }

    bool parse_drop(Stmt& s) {
        if (!expect_kw("TABLE")) return false;
        if (!parse_name(s.table)) return false;
        return true;
    }
};

bool sql_parse(const char* sql, Stmt& out, String* err) {
    if (!sql) { if (err) *err = String("空语句"); return false; }
    Parser p;
    p.err = err;
    lex(sql, p.toks);
    if (p.toks.size() <= 1) { if (err) *err = String("空语句"); return false; }

    // 根据第一个关键字分派
    Token& first = p.peek();
    if (first.kind != TOK_ID) { if (err) *err = String("语句必须以关键字开头"); return false; }

    if (kw(first.text, "CREATE")) {
        p.next();
        out.kind = STMT_CREATE;
        if (!p.parse_create(out)) return false;
    } else if (kw(first.text, "INSERT")) {
        p.next();
        out.kind = STMT_INSERT;
        if (!p.parse_insert(out)) return false;
    } else if (kw(first.text, "SELECT")) {
        p.next();
        out.kind = STMT_SELECT;
        if (!p.parse_select(out)) return false;
    } else if (kw(first.text, "DELETE")) {
        p.next();
        out.kind = STMT_DELETE;
        if (!p.parse_delete(out)) return false;
    } else if (kw(first.text, "UPDATE")) {
        p.next();
        out.kind = STMT_UPDATE;
        if (!p.parse_update(out)) return false;
    } else if (kw(first.text, "DROP")) {
        p.next();
        out.kind = STMT_DROP;
        if (!p.parse_drop(out)) return false;
    } else {
        if (err) *err = String("未知语句类型: ") + first.text;
        return false;
    }

    // 可选结尾分号
    if (p.peek().kind == TOK_SYM && p.peek().text == ";") p.next();
    if (!p.eof()) { if (err) *err = String("语句末尾有多余内容"); return false; }
    return true;
}

// ===================== 自检 =====================
int sqlparse_self_test() {
    int fail = 0;
    String err;

    // 1) CREATE TABLE
    {
        Stmt s;
        if (!sql_parse("CREATE TABLE users (id, name, age)", s, &err)) fail++;
        if (s.kind != STMT_CREATE) fail++;
        if (s.table != "users") fail++;
        if (s.create_cols.size() != 3) fail++;
        if (s.create_cols[0] != "id" || s.create_cols[2] != "age") fail++;
    }

    // 2) INSERT
    {
        Stmt s;
        if (!sql_parse("INSERT INTO users VALUES (1, 'alice', 20)", s, &err)) fail++;
        if (s.kind != STMT_INSERT) fail++;
        if (s.table != "users") fail++;
        if (s.insert_values.size() != 3) fail++;
        if (s.insert_values[0] != "1" || s.insert_values[1] != "alice" || s.insert_values[2] != "20") fail++;
    }

    // 3) SELECT * FROM
    {
        Stmt s;
        if (!sql_parse("SELECT * FROM users", s, &err)) fail++;
        if (s.kind != STMT_SELECT) fail++;
        if (s.sel_cols.size() != 0) fail++;       // 空 = *
        if (s.table != "users") fail++;
    }

    // 4) SELECT 列 + WHERE + ORDER BY + LIMIT
    {
        Stmt s;
        if (!sql_parse("SELECT name, age FROM users WHERE age >= 18 ORDER BY age LIMIT 10", s, &err)) fail++;
        if (s.sel_cols.size() != 2) fail++;
        if (s.sel_cols[0] != "name") fail++;
        if (s.where.or_of_ands.size() != 1) fail++;
        if (s.where.or_of_ands[0].size() != 1) fail++;
        CmpCond& c = s.where.or_of_ands[0][0];
        if (c.col != "age" || c.op != CMP_GE || c.value != "18") fail++;
        if (!s.has_order || s.order_col != "age") fail++;
        if (!s.has_limit || s.limit != 10) fail++;
    }

    // 5) WHERE AND / OR
    {
        Stmt s;
        if (!sql_parse("SELECT * FROM t WHERE a = 1 AND b < 5 OR c >= 10", s, &err)) fail++;
        // (a=1 AND b<5) OR (c>=10)
        if (s.where.or_of_ands.size() != 2) fail++;
        if (s.where.or_of_ands[0].size() != 2) fail++;
        if (s.where.or_of_ands[1].size() != 1) fail++;
    }

    // 6) DELETE / UPDATE / DROP
    {
        Stmt d;
        if (!sql_parse("DELETE FROM users WHERE id = 3", d, &err)) fail++;
        if (d.kind != STMT_DELETE || d.table != "users") fail++;
        if (d.where.or_of_ands[0][0].col != "id" || d.where.or_of_ands[0][0].value != "3") fail++;

        Stmt u;
        if (!sql_parse("UPDATE users SET name = 'bob' WHERE id = 1", u, &err)) fail++;
        if (u.kind != STMT_UPDATE || u.set_col != "name" || u.set_value != "bob") fail++;

        Stmt dr;
        if (!sql_parse("DROP TABLE users", dr, &err)) fail++;
        if (dr.kind != STMT_DROP || dr.table != "users") fail++;
    }

    // 7) 大小写不敏感 + 分号结尾
    {
        Stmt s;
        if (!sql_parse("select * from USERS where age<>20;", s, &err)) fail++;
        if (s.kind != STMT_SELECT || s.table != "USERS") fail++;
        if (s.where.or_of_ands[0][0].op != CMP_NE) fail++;
    }

    // 8) 语法错误应返回 false
    {
        Stmt s;
        if (sql_parse("SELECT FROM", s, &err)) fail++;
    }

    return fail;
}

} // namespace database
} // namespace nefu
