// nefuOS 数据序列化与编解码库 —— 文本格式模块
// textfmt.h: CSV / INI / XML / TOML / JSON5 / .properties / .env / S-expression
//
// 设计目标（教学友好）：
//   * 全部从零手写，不依赖 STL、不依赖异常、不依赖 RTTI；
//   * 内存一律走 nefu::String / nefu::List / new[]-delete[]（底层即 kalloc/kfree）；
//   * 每个格式都提供「解析(parse) + 生成(write/to_string)」双向能力；
//   * 每个模块末尾都有 xxx_self_test()，返回失败断言数（0 表示全部通过）。
//
// 本头文件只暴露数据结构与函数声明；具体算法在 textfmt.cpp。
#pragma once
#include "../klib/klib.h"

namespace nefu {
namespace serialize {

// ============================================================================
// 0. 通用动态值（供 TOML / JSON5 / S-expression 复用）
//    这是一棵自包含的树：对象按插入顺序保存成员，数组顺序保存元素。
// ============================================================================
enum class VType {
    V_NULL = 0,   // 空值
    V_BOOL,       // 布尔
    V_INT,        // 整数（int64）
    V_DOUBLE,     // 浮点（double）
    V_STRING,     // 字符串（String）
    V_ARRAY,      // 数组（List<VRef>）
    V_OBJECT      // 对象（List<VMember>，有序）
};

struct VMember;   // 前向声明

// 注意：因为 List<T> 需要完整类型才能实例化，但 VArray 内部用指针数组
// （List<VValue*>）以避免递归不完整类型问题。对象同理。
struct VValue {
    VType type;
    bool b;                 // V_BOOL
    long long i;            // V_INT
    double d;               // V_DOUBLE
    String s;               // V_STRING
    // V_ARRAY / V_OBJECT：用裸指针列表持有子节点（自管理生命周期）
    List<VValue*> arr;      // V_ARRAY 的元素
    List<VMember> obj;      // V_OBJECT 的成员

    VValue() : type(VType::V_NULL), b(false), i(0), d(0.0) {}
    VValue(bool v) : type(VType::V_BOOL), b(v), i(0), d(0.0) {}
    VValue(long long v) : type(VType::V_INT), b(false), i(v), d(0.0) {}
    VValue(int v) : type(VType::V_INT), b(false), i(v), d(0.0) {}
    VValue(double v) : type(VType::V_DOUBLE), b(false), i(0), d(v) {}
    VValue(const char* v) : type(VType::V_STRING), b(false), i(0), d(0.0), s(v) {}
    VValue(const String& v) : type(VType::V_STRING), b(false), i(0), d(0.0), s(v) {}

    // 谓词
    bool is_null() const { return type == VType::V_NULL; }
    bool is_bool() const { return type == VType::V_BOOL; }
    bool is_int() const { return type == VType::V_INT; }
    bool is_double() const { return type == VType::V_DOUBLE; }
    bool is_string() const { return type == VType::V_STRING; }
    bool is_array() const { return type == VType::V_ARRAY; }
    bool is_object() const { return type == VType::V_OBJECT; }

    int size() const;                       // 数组/对象元素数
    const VValue* get(const char* key) const;   // 对象查找（大小写敏感）
    VValue* get(const char* key);
    bool has(const char* key) const;
    void set(const char* key, const VValue& v); // 对象：设置/新增成员
    void push(const VValue& v);                  // 数组：追加元素

    // 深拷贝工厂：parse 出来的树由调用者用 free_value() 释放
    static VValue* clone(const VValue& o);
};

struct VMember {
    String key;
    VValue* val;   // 拥有
    VMember() : val(0) {}
    VMember(const String& k, VValue* v) : key(k), val(v) {}
};

// 递归释放一棵 VValue 树（含其数组/对象里的所有子节点）
void free_value(VValue* v);

// ============================================================================
// 1. CSV —— 逗号分隔值
//    支持：引号包裹字段、字段内逗号/双引号("" 转义)、字段内换行（多行字段）、
//          可配置分隔符（默认逗号）、CRLF 与 LF 行结束符。
// ============================================================================
struct CsvDoc {
    List<List<String>> rows;   // 二维表：rows[r][c]
    char delim;                // 分隔符，默认 ','

    CsvDoc() : delim(',') {}
    int rows_count() const { return rows.size(); }
    int cols_count() const;    // 最大列数（各行可能不齐）
    // 取第 r 行第 c 列；越界返回 ""
    const char* cell(int r, int c) const;
};

// 解析整段 CSV 文本。失败返回 false（*err 可空，输出错误描述）。
bool csv_parse(const char* text, CsvDoc& out, char delim = ',', const char** err = 0);
// 把表格生成为 CSV 文本（必要时自动加引号/转义）。CRLF 行结束。
void csv_write(const CsvDoc& doc, String& out);
int  csv_self_test();

// ============================================================================
// 2. INI —— Windows-style 配置文件
//    结构：[section] 行，其后 key = value 直到下一个 section。
//    注释：行首 ';' 或 '#'；值支持 '#' 内联注释（可关）；键值分隔符 '=' 或 ':'。
// ============================================================================
struct IniEntry {
    String key;
    String value;
    String comment;   // 该行末尾注释（不含 '#'），无则空
};
struct IniSection {
    String name;              // 空串表示“全局区”（首行 [section] 之前）
    List<IniEntry> entries;
};
struct IniDoc {
    List<IniSection> sections;
    // 按名字找 section（找不到返回 0）
    IniSection* find_section(const char* name);
    // 跨 section 取值；找不到返回 def
    const char* get(const char* section, const char* key, const char* def = "");
    void set(const char* section, const char* key, const char* value);
};

bool ini_parse(const char* text, IniDoc& out, const char** err = 0);
void ini_write(const IniDoc& doc, String& out);
int  ini_self_test();

// ============================================================================
// 3. XML —— 子集 DOM 解析与生成
//    支持：开始/结束标签、自闭合标签、属性(key="v")、命名空间前缀(prefix:local)、
//          CDATA(<![CDATA[ ... ]]>)、注释(<!-- -->)、处理指令忽略、
//          实体 &lt; &gt; &quot; &apos; &amp; &#NN;。
//    不支持：DTD、外部实体、命名空间 URI 解析（只保留前缀字符串）。
// ============================================================================
struct XmlAttr {
    String name;   // 可能含前缀，如 xml:lang
    String value;
};
struct XmlNode {
    String name;        // 元素标签名（可能含前缀）
    String ns_prefix;   // 命名空间前缀（':' 之前），无则空
    List<XmlAttr> attrs;
    List<XmlNode*> children;   // 子元素（拥有）
    String text;               // 该节点的直接文本内容（相邻文本会合并）
    bool is_cdata_leaf;        // 仅用于生成时保留 CDATA（解析后并入 text）

    XmlNode() : is_cdata_leaf(false) {}
    XmlNode* child(const char* name) const;     // 第一个同名子元素
    XmlNode* add_child(const char* name);        // 新建并挂一个子元素
    const char* attr(const char* name, const char* def = "") const;
    void set_attr(const char* name, const char* value);
};

struct XmlDoc {
    XmlNode* root;   // 拥有
    String version;  // <?xml version="1.0"?> 中的 version
    String encoding;
    XmlDoc() : root(0) {}
};

bool xml_parse(const char* text, XmlDoc& out, const char** err = 0);
// 生成 XML；pretty=true 时缩进美化。
void xml_write(const XmlDoc& doc, String& out, bool pretty = false);
void xml_free(XmlDoc& doc);   // 释放整棵 DOM
int  xml_self_test();

// ============================================================================
// 4. TOML —— 最小可用子集
//    支持：[table]、[[array of tables]]、key = value、#注释、
//          基本字符串 "..."（转义）/字面字符串 '...'（不转义）、
//          多行字符串 """..."""、整数/浮点/布尔、数组 [1,2,3]、内联表 {a=1,b=2}、
//          日期时间（只保留原始文本，不做数值解析）。
// ============================================================================
struct TomlDoc {
    VValue* root;   // V_OBJECT，拥有
    TomlDoc() : root(0) {}
};

bool toml_parse(const char* text, TomlDoc& out, const char** err = 0);
void toml_write(const VValue& root, String& out, bool pretty = true);
int  toml_self_test();

// ============================================================================
// 5. JSON5 —— JSON 超集解析器（输出统一的 VValue 树）
//    支持：// 与 /* */ 注释、尾逗号、单引号字符串、无引号标识符键、
//          十六进制 0x..、前导/尾随小数点、+ 号、NaN/Infinity。
//    注意：本实现自包含，不复用 core/lib/json.cpp。
// ============================================================================
// 解析 JSON5 文本，返回一棵需调用 free_value() 释放的树（失败返回 0）。
VValue* json5_parse(const char* text, const char** err = 0);
// 把 VValue 树写成 JSON（标准 JSON，不带 JSON5 语法）。
void   json5_write(const VValue& v, String& out, bool pretty = false);
int    json5_self_test();

// ============================================================================
// 6. .properties —— Java 属性文件
//    规则：key[=|:]value；# 或 ! 开头为注释；行尾反斜杠续行；
//          \uXXXX Unicode 转义；\t \n \r \\ 等转义。
// ============================================================================
struct PropertiesDoc {
    List<IniEntry> entries;   // 保持出现顺序
    const char* get(const char* key, const char* def = "");
    void set(const char* key, const char* value);
};

bool properties_parse(const char* text, PropertiesDoc& out, const char** err = 0);
void properties_write(const PropertiesDoc& doc, String& out);
int  properties_self_test();

// ============================================================================
// 7. .env —— 环境变量文件
//    规则：export? KEY=VALUE；# 注释；值可用单/双引号包裹（保留内部空格）；
//          不做变量展开（$VAR 原样保留）。
// ============================================================================
struct EnvDoc {
    List<IniEntry> entries;
    const char* get(const char* key, const char* def = "");
};

bool env_parse(const char* text, EnvDoc& out, const char** err = 0);
void env_write(const EnvDoc& doc, String& out);
int  env_self_test();

// ============================================================================
// 8. S-expression —— 符号表达式解析
//    语法：(a b (c "d" e) 123)；支持 "..." 字符串、注释 ; 到行尾、
//          整数与浮点原子、引号 'quote 前缀。输出统一 VValue 树。
// ============================================================================
VValue* sexpr_parse(const char* text, const char** err = 0);
void    sexpr_write(const VValue& v, String& out);   // 紧凑打印
int     sexpr_self_test();

// ============================================================================
// 汇总：运行所有文本格式自检测，返回失败总数。
// ============================================================================
int textfmt_self_test();

} // namespace serialize
} // namespace nefu
// textfmt.h: 文本序列化集合（CSV/INI/XML/TOML/JSON5/properties/.env/S-expression）。
// 命名空间 nefu::serialize。
// end.
