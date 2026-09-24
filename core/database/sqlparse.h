// nefuOS 嵌入式数据库 —— 迷你 SQL 解析器
//
// 分两步:
//   1) 词法分析(lexer):把 SQL 字符串切成 token 流(关键字 / 标识符 / 数字 / 字符串 / 运算符)。
//   2) 语法分析(parser):递归下降,把 token 流归约成语法树(AST)。
//
// 支持的语句:
//   CREATE TABLE t (c1, c2, ...);
//   INSERT INTO t VALUES (v1, v2, ...);
//   SELECT [*, c1, c2] FROM t [WHERE cond] [ORDER BY c] [LIMIT n];
//   DELETE FROM t [WHERE cond];
//   UPDATE t SET c = v [WHERE cond];
//   DROP TABLE t;
//
// WHERE 条件:col (= | != | < | > | <= | >=) 字面量,可用 AND / OR 连接。
// 不依赖 STL / 异常 / RTTI。
#pragma once
#include "../klib/klib.h"

namespace nefu {
namespace database {

// ---- 比较运算符 ----
enum CmpOp {
    CMP_EQ, CMP_NE, CMP_LT, CMP_GT, CMP_LE, CMP_GE
};

// ---- 一个比较条件:col op value(value 以文本形式存放) ----
struct CmpCond {
    String col;
    CmpOp  op;
    String value;
};

// WHERE 条件 = 若干 "AND 组" 用 OR 连接:
//   (a=1 AND b<2) OR (c>=5)
// 每个 List<CmpCond> 是一个 AND 组;组之间是 OR。
struct WhereExpr {
    List<List<CmpCond>> or_of_ands;   // 外层 OR,内层 AND
};

// ---- 语句类型 ----
enum StmtKind {
    STMT_CREATE, STMT_INSERT, STMT_SELECT,
    STMT_DELETE, STMT_UPDATE, STMT_DROP
};

// ---- 语法树节点(一条语句) ----
struct Stmt {
    StmtKind kind;
    String   table;                 // 目标表名

    // CREATE TABLE:列名列表
    List<String> create_cols;

    // INSERT:VALUES 列表(与表列按位置对应)
    List<String> insert_values;

    // SELECT:要输出的列(空 = *)
    List<String> sel_cols;
    bool   has_order;
    String order_col;
    bool   has_limit;
    int    limit;

    // WHERE(DELETE / UPDATE / SELECT 共用)
    WhereExpr where;

    // UPDATE:SET col = value
    String set_col;
    String set_value;

    Stmt() : kind(STMT_SELECT), has_order(false), has_limit(false), limit(0) {}
};

// ---- 解析器 ----
// parse():解析一条 SQL 语句到 out。成功返回 true,失败返回 false 并把
//         可读的错误信息写进 err(若 err 非空)。
// 注意:out 由调用者分配,parse 会填充其内容;之后调用者直接使用即可,
//       Stmt 内的 List/String 自带析构,无需手动 free。
bool sql_parse(const char* sql, Stmt& out, String* err);

// 把运算符枚举转回符号(调试 / 打印用)
const char* cmp_op_str(CmpOp op);

// 模块自检:返回失败断言数(0 表示全部通过)
int sqlparse_self_test();

} // namespace database
} // namespace nefu
