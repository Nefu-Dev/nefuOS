// ============================================================================
// nefu::compiler —— C 预处理器实现（preprocessor.cpp）
// ============================================================================
#include "preprocessor.h"
#include <string.h>
#include <ctype.h>

namespace nefu {
namespace compiler {

Preprocessor::Preprocessor() : cond_depth(0), err_count(0) {}
Preprocessor::~Preprocessor() {
    for (int i = 0; i < macros.size(); i++) delete macros[i];
}

Macro* Preprocessor::find(const char* name) {
    for (int i = 0; i < macros.size(); i++) {
        if (macros[i]->name == name) return macros[i];
    }
    return 0;
}

void Preprocessor::define_object(const char* name, const char* body) {
    Macro* m = new Macro();
    m->name = name;
    m->body = body;
    m->is_func = false;
    macros.push(m);
}

void Preprocessor::define_func(const char* name, const char* params_csv, const char* body) {
    Macro* m = new Macro();
    m->name = name;
    m->body = body;
    m->is_func = true;
    // 解析逗号分隔参数
    String cur;
    for (const char* p = params_csv; *p; p++) {
        if (*p == ',') { m->params.push(cur); cur = String(); }
        else if (!isspace((unsigned char)*p)) cur += *p;
    }
    if (!cur.empty()) m->params.push(cur);
    macros.push(m);
}

void Preprocessor::undef(const char* name) {
    for (int i = 0; i < macros.size(); i++) {
        if (macros[i]->name == name) {
            delete macros[i];
            macros.remove(i);
            return;
        }
    }
}

// 判断某行当前是否应输出（条件栈最顶层是否 active）
static bool line_active(List<int>& cond_active) {
    for (int i = 0; i < cond_active.size(); i++)
        if (!cond_active[i]) return false;
    return true;
}

String Preprocessor::expand(const char* line) {
    String result;
    const char* p = line;
    while (*p) {
        // 读取一个单词
        if (isalnum((unsigned char)*p) || *p == '_') {
            String word;
            while (isalnum((unsigned char)*p) || *p == '_') { word += *p; p++; }
            // 跳过空白看是否 '('（带参宏）
            const char* q = p;
            while (isspace((unsigned char)*q)) q++;
            Macro* m = find(word.c_str());
            if (m && m->is_func && *q == '(') {
                // 收集实参（简单：按逗号切到 ')'）
                q++;
                List<String> args;
                String arg;
                int depth = 1;
                while (*q && depth > 0) {
                    if (*q == '(') depth++;
                    else if (*q == ')') { depth--; if (depth == 0) { args.push(arg); break; } }
                    else if (*q == ',' && depth == 1) { args.push(arg); arg = String(); q++; continue; }
                    arg += *q;
                    q++;
                }
                p = q + 1; // 跳过 ')'
                // 替换 body 中参数
                String body = m->body;
                for (int i = 0; i < m->params.size() && i < args.size(); i++) {
                    // 把参数名替换为实参（朴素字符串替换）
                    String rep;
                    const char* ph = m->params[i].c_str();
                    int phlen = m->params[i].len();
                    for (int k = 0; k <= body.len(); ) {
                        if (k + phlen <= body.len() && strncmp(body.c_str() + k, ph, phlen) == 0) {
                            rep += args[i];
                            k += phlen;
                        } else { rep += body[k]; k++; }
                    }
                    body = rep;
                }
                result += body;
            } else if (m && !m->is_func) {
                result += m->body;
            } else {
                result += word;
            }
        } else {
            result += *p;
            p++;
        }
    }
    return result;
}

// 预处理第一遍：把行尾反斜杠+换行合并为一行
static String join_line_continuations(const char* src) {
    String s;
    for (const char* p = src; *p; p++) {
        if (*p == '\\' && *(p + 1) == '\n') { p++; continue; }
        if (*p == '\\' && *(p + 1) == '\r' && *(p + 2) == '\n') { p += 2; continue; }
        s += *p;
    }
    return s;
}

// ---- #if 常量表达式求值器（教学子集）----
// 支持：整数、已定义对象宏(取其替换文本的数值)、! && ||、比较 > < >= <= == !=、+ - * /
// 求值失败返回默认值 def_when_error。
// 简易整数解析（避免依赖 strtol）
static int myatoi(const char* t) {
    int sign = 1, v = 0;
    while (*t == ' ' || *t == '\t') t++;
    if (*t == '-') { sign = -1; t++; }
    while (*t >= '0' && *t <= '9') { v = v * 10 + (*t - '0'); t++; }
    return sign * v;
}
struct CtxEval {
    const char* s; int i; Preprocessor* pp;
    int peek() { return s[i]; }
    void skip(){ while(s[i]==' '||s[i]=='\t') i++; }
    int parse_primary() {
        skip();
        if (s[i]=='('){ i++; int v=parse_or(); skip(); if(s[i]==')')i++; return v; }
        if (s[i]=='!'){ i++; return parse_primary()?0:1; }
        if (s[i]=='-'){ i++; return -parse_primary(); }
        // 标识符或数字
        int start=i;
        if (isalpha((unsigned char)s[i])||s[i]=='_'){
            while(isalnum((unsigned char)s[i])||s[i]=='_') i++;
            String nm; for(int k=start;k<i;k++) nm+=s[k];
            Macro* m = pp->find(nm.c_str());
            if(!m) return 0;                 // 未定义宏在 #if 中为 0
            return myatoi(m->body.c_str());
        }
        int v = myatoi(s+i);
        while (s[i] >= '0' && s[i] <= '9') i++;
        return v;
    }
    int parse_mul(){ int v=parse_primary(); skip();
        while(peek()=='*'||peek()=='/'||peek()=='%'){ char op=s[i++]; int r=parse_primary();
            v=(op=='*')?v*r:(op=='/')?(r?v/r:0):(r?v%r:0); skip(); } return v; }
    int parse_add(){ int v=parse_mul(); skip();
        while(peek()=='+'||peek()=='-'){ char op=s[i++]; int r=parse_mul(); v=(op=='+')?v+r:v-r; skip(); } return v; }
    int parse_cmp(){ int v=parse_add(); skip();
        while(peek()=='<'||peek()=='>'||(s[i]=='='&&s[i+1]=='=')||(s[i]=='!'&&s[i+1]=='=')){
            int op; if(s[i]=='='&&s[i+1]=='='){op=1;i+=2;} else if(s[i]=='!'&&s[i+1]=='='){op=2;i+=2;} else {op=(s[i]=='<')?3:4;i++;}
            int r=parse_add(); int res=0;
            if(op==1)res=(v==r); else if(op==2)res=(v!=r); else if(op==3)res=(v<r); else res=(v>r);
            v=res; skip(); } return v; }
    int parse_and(){ int v=parse_cmp(); skip();
        while(s[i]=='&'&&s[i+1]=='&'){ i+=2; int r=parse_cmp(); v=v&&r; skip(); } return v; }
    int parse_or(){ int v=parse_and(); skip();
        while(s[i]=='|'&&s[i+1]=='|'){ i+=2; int r=parse_and(); v=v||r; skip(); } return v; }
};
static int eval_if_expr(Preprocessor* pp, const char* expr) {
    CtxEval e; e.s=expr; e.i=0; e.pp=pp;
    return e.parse_or();
}
const char* Preprocessor::process(const char* source) {
    String joined = join_line_continuations(source);
    source = joined.c_str();
    out = String();
    cond_depth = 0;
    cond_active.clear();
    String line;
    for (const char* p = source; ; p++) {
        char c = *p;
        if (c == '\n' || c == 0) {
            // 处理一行
            // 去掉行首空白
            int start = 0;
            while (start < line.len() && isspace((unsigned char)line[start])) start++;
            String trimmed = line.substr(start, line.len() - start);

            if (trimmed.len() > 0 && trimmed[0] == '#') {
                // 指令
                if (strncmp(trimmed.c_str(), "#define", 7) == 0) {
                    // 解析 #define NAME[(params)] body
                    int i = 7;
                    while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                    String name;
                    while (i < trimmed.len() && (isalnum((unsigned char)trimmed[i]) || trimmed[i] == '_')) {
                        name += trimmed[i]; i++;
                    }
                    if (i < trimmed.len() && trimmed[i] == '(') {
                        // 带参宏
                        i++;
                        String params;
                        while (i < trimmed.len() && trimmed[i] != ')') { params += trimmed[i]; i++; }
                        i++; // 跳过 )
                        while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                        String body = trimmed.substr(i, trimmed.len() - i);
                        define_func(name.c_str(), params.c_str(), body.c_str());
                    } else {
                        while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                        String body = trimmed.substr(i, trimmed.len() - i);
                        define_object(name.c_str(), body.c_str());
                    }
                } else if (strncmp(trimmed.c_str(), "#undef", 6) == 0) {
                    int i = 6;
                    while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                    String name;
                    while (i < trimmed.len() && (isalnum((unsigned char)trimmed[i]) || trimmed[i] == '_')) {
                        name += trimmed[i]; i++;
                    }
                    undef(name.c_str());
                } else if (strncmp(trimmed.c_str(), "#ifdef", 6) == 0) {
                    int i = 6;
                    while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                    String name;
                    while (i < trimmed.len() && (isalnum((unsigned char)trimmed[i]) || trimmed[i] == '_')) {
                        name += trimmed[i]; i++;
                    }
                    bool def = find(name.c_str()) != 0;
                    cond_active.push(line_active(cond_active) && def ? 1 : 0);
                    cond_depth++;
                } else if (strncmp(trimmed.c_str(), "#ifndef", 7) == 0) {
                    int i = 7;
                    while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                    String name;
                    while (i < trimmed.len() && (isalnum((unsigned char)trimmed[i]) || trimmed[i] == '_')) {
                        name += trimmed[i]; i++;
                    }
                    bool def = find(name.c_str()) != 0;
                    cond_active.push(line_active(cond_active) && !def ? 1 : 0);
                    cond_depth++;
                } else if (strncmp(trimmed.c_str(), "#if", 3) == 0 && trimmed.len() > 3 && trimmed[3] != 'd') {
                    // #if [!]defined NAME：求值宏是否存在
                    int i = 3;
                    bool neg = false;
                    while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                    if (i < trimmed.len() && trimmed[i] == '!') { neg = true; i++; }
                    while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                    if (strncmp(trimmed.c_str() + i, "defined", 7) == 0) {
                        i += 7;
                        while (i < trimmed.len() && (isspace((unsigned char)trimmed[i]) || trimmed[i] == '(')) i++;
                        String name;
                        while (i < trimmed.len() && (isalnum((unsigned char)trimmed[i]) || trimmed[i] == '_')) {
                            name += trimmed[i]; i++;
                        }
                        bool def = find(name.c_str()) != 0;
                        bool active = neg ? !def : def;
                        cond_active.push(line_active(cond_active) && active ? 1 : 0);
                        cond_taken.push(active ? 1 : 0);
                        cond_depth++;
                    } else {
                        // 用常量表达式求值器计算 #if 条件
                        String expr;
                        for (int k = i; k < trimmed.len(); k++) expr += trimmed[k];
                        bool active = eval_if_expr(this, expr.c_str()) != 0;
                        cond_active.push(line_active(cond_active) && active ? 1 : 0);
                        cond_taken.push(active ? 1 : 0);
                        cond_depth++;
                    }
                } else if (strncmp(trimmed.c_str(), "#elif", 5) == 0) {
                    if (cond_depth > 0) {
                        int idx = cond_depth - 1;
                        if (cond_taken[idx]) {
                            cond_active[idx] = 0;      // 已有分支命中，关闭本层
                        } else {
                            // #elif defined NAME
                            int i = 5;
                            while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                            if (strncmp(trimmed.c_str() + i, "defined", 7) == 0) {
                                i += 7;
                                while (i < trimmed.len() && (isspace((unsigned char)trimmed[i]) || trimmed[i] == '(')) i++;
                                String name;
                                while (i < trimmed.len() && (isalnum((unsigned char)trimmed[i]) || trimmed[i] == '_')) {
                                    name += trimmed[i]; i++;
                                }
                                bool def = find(name.c_str()) != 0;
                                cond_active[idx] = (line_active(cond_active) && def) ? 1 : 0;
                                if (def) cond_taken[idx] = 1;
                            }
                        }
                    }
                } else if (strncmp(trimmed.c_str(), "#else", 5) == 0) {
                    if (cond_depth > 0) {
                        int idx = cond_depth - 1;
                        cond_active[idx] = cond_taken[idx] ? 0 : 1;
                        cond_taken[idx] = 1;
                    }
                } else if (strncmp(trimmed.c_str(), "#endif", 6) == 0) {
                    if (cond_depth > 0) {
                        cond_active.remove(cond_depth - 1);
                        cond_taken.remove(cond_depth - 1);
                        cond_depth--;
                    }
                } else if (strncmp(trimmed.c_str(), "#error", 6) == 0) {
                    err_count++;
                    int i = 6;
                    while (i < trimmed.len() && isspace((unsigned char)trimmed[i])) i++;
                    errors.push(trimmed.substr(i, trimmed.len() - i));
                } else if (strncmp(trimmed.c_str(), "#include", 8) == 0) {
                    // 模拟：记录路径
                    int i = 8;
                    String path;
                    while (i < trimmed.len()) {
                        if (trimmed[i] != '"' && trimmed[i] != '<' && trimmed[i] != '>') path += trimmed[i];
                        i++;
                    }
                    includes.push(path);
                }
            } else {
                if (line_active(cond_active)) {
                    out += expand(line.c_str());
                    out += "\n";
                }
            }
            line = String();
            if (c == 0) break;
        } else {
            line += c;
        }
    }
    return out.c_str();
}

// ---- 自测试 ----
int preprocessor_self_test() {
    int fails = 0;

    // 对象式宏展开
    {
        Preprocessor pp;
        const char* src =
            "#define N 10\n"
            "int x = N + N;\n";
        pp.process(src);
        if (pp.out.find("10 + 10") < 0 && pp.out.find("20") < 0) fails++;
        if (pp.out.find("N") >= 0) fails++;     // N 应被展开
    }

    // 带参宏
    {
        Preprocessor pp;
        const char* src =
            "#define SQR(x) ((x)*(x))\n"
            "int y = SQR(3);\n";
        pp.process(src);
        if (pp.out.find("((3)*(3))") < 0) fails++;
    }

    // 条件编译：未定义 DEBUG 时排除
    {
        Preprocessor pp;
        const char* src =
            "#ifdef DEBUG\n"
            "int dbg = 1;\n"
            "#endif\n"
            "int real = 2;\n";
        pp.process(src);
        if (pp.out.find("dbg") >= 0) fails++;     // 不应出现
        if (pp.out.find("real") < 0) fails++;
    }

    // 条件编译：定义 DEBUG 后保留
    {
        Preprocessor pp;
        pp.define_object("DEBUG", "1");
        const char* src =
            "#ifdef DEBUG\n"
            "int dbg = 1;\n"
            "#endif\n";
        pp.process(src);
        if (pp.out.find("dbg") < 0) fails++;
    }

    // #include 记录
    {
        Preprocessor pp;
        const char* src = "#include \"kernel.h\"\nint a;\n";
        pp.process(src);
        if (pp.includes.size() != 1) fails++;
    }

    // #undef
    {
        Preprocessor pp;
        const char* src =
            "#define N 5\n"
            "#undef N\n"
            "int a = N;\n";
        pp.process(src);
        // N 被 undef 后不再展开，输出应仍含 N
        if (pp.out.find("N") < 0) fails++;
    }

    // #if defined / #elif / #else
    {
        Preprocessor pp;
        pp.define_object("VERSION", "2");
        const char* src =
            "#if defined(VERSION)\n"
            "int v = 1;\n"
            "#elif defined(BETA)\n"
            "int b = 1;\n"
            "#else\n"
            "int z = 1;\n"
            "#endif\n";
        pp.process(src);
        if (pp.out.find("v = 1") < 0) fails++;
        if (pp.out.find("b = 1") >= 0) fails++;
        if (pp.out.find("z = 1") >= 0) fails++;
    }

    // #if !defined：未定义时取反命中
    {
        Preprocessor pp;
        const char* src =
            "#if !defined(FEATURE)\n"
            "int off = 1;\n"
            "#endif\n";
        pp.process(src);
        if (pp.out.find("off") < 0) fails++;
    }

    // #error 收集
    {
        Preprocessor pp;
        const char* src =
            "#error something wrong\n"
            "int a;\n";
        pp.process(src);
        if (pp.err_count != 1) fails++;
        if (pp.errors.size() != 1) fails++;
    }

    // 嵌套条件编译
    {
        Preprocessor pp;
        pp.define_object("A", "1");
        const char* src =
            "#ifdef A\n"
            "#ifdef B\n"
            "int deep = 1;\n"
            "#endif\n"
            "int outer = 1;\n"
            "#endif\n";
        pp.process(src);
        if (pp.out.find("deep") >= 0) fails++;
        if (pp.out.find("outer") < 0) fails++;
    }

    // 宏计数：define 后 +1，undef 后 -1
    {
        Preprocessor pc;
        int before = pc.macro_count();
        pc.define_object("ZZ", "1");
        if (pc.macro_count() != before + 1) fails++;
        pc.undef("ZZ");
        if (pc.macro_count() != before) fails++;
    }

    // #error：应收集错误信息
    {
        Preprocessor pp;
        const char* src = "#error boom\nint a = 1;\n";
        pp.process(src);
        if (pp.errors.size() != 1) fails++;
    }

    // 带参宏：#define SQR(x) ((x)*(x))
    {
        Preprocessor pp;
        pp.define_func("SQR", "x", "((x)*(x))");
        const char* src = "int a = SQR(3);\n";
        pp.process(src);
        if (pp.out.find("((3)*(3))") < 0) fails++;
    }

    // #undef：取消宏定义后不再展开
    {
        Preprocessor pp;
        pp.define_object("M", "123");
        pp.undef("M");
        const char* src = "int a = M;\n";
        pp.process(src);
        if (pp.out.find("123") >= 0) fails++;     // M 已 undef，不应展开
    }

    // #if 数值表达式求值：N=5，#if N>3 取真，#if N<2 取假
    {
        Preprocessor pp;
        pp.define_object("N", "5");
        const char* src =
            "#if N > 3\n"
            "int big = 1;\n"
            "#endif\n"
            "#if N < 2\n"
            "int small = 1;\n"
            "#endif\n";
        pp.process(src);
        if (pp.out.find("big") < 0) fails++;        // 应保留
        if (pp.out.find("small") >= 0) fails++;   // 应剔除
    }

    // 行续接：define 跨两行应合并
    {
        Preprocessor pp;
        const char* src =
            "#define LONG 1 + \\\n"
            "2\n"
            "int a = LONG;\n";
        pp.process(src);
        if (pp.out.find("1 + 2") < 0 && pp.out.find("3") < 0) fails++;
    }

    return fails;
}

} // namespace compiler
} // namespace nefu
