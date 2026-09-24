// ============================================================================
// nefu::compiler —— 三地址中间代码（ir.h）
// ----------------------------------------------------------------------------
// 线性指令流 + 虚拟寄存器 + 命名标签。每条指令最多三个操作数，便于后续
// codegen 逐条翻译为 x86-64 汇编。包含：
//   * 指令操作码、虚拟寄存器分配、标签管理
//   * 从 AST 翻译为 IR 的生成器（ast -> ir）
//   * IR 文本打印（供窗口应用展示）
// ============================================================================
#pragma once

#include "ast.h"

namespace nefu {
namespace compiler {

// ---- IR 指令操作码 ----
enum IrOp {
    IR_NOP = 0,
    IR_CONST,        // v = 立即数
    IR_MOVE,         // v = src
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SHR,
    IR_NEG, IR_NOT,
    IR_CMP,          // v = (a cmp b)，比较关系在 cmp_op 里
    IR_BR,           // 无条件跳转 label
    IR_BRCOND,       // if v 真跳 label_true else label_false
    IR_LABEL,        // 标签锚点
    IR_CALL,         // v = call name(args...)
    IR_RET,          // return v（可空）
    IR_PARAM,        // 把第 i 个参数放进 v
    IR_ALLOCA,       // v = 栈槽（局部变量地址）
    IR_LOAD,         // v = [addr]
    IR_STORE,        // [addr] = v
    IR_GEP           // v = addr + idx*scale（下标取址）
};

// ---- 比较关系（IR_CMP 用）----
enum CmpRel {
    CR_EQ = 0, CR_NE, CR_LT, CR_LE, CR_GT, CR_GE
};

struct IrInstr {
    int op;             // IrOp
    int dest;           // 目标虚拟寄存器（-1 表示无）
    int a, b;           // 源操作数（vreg 或立即数，按 opcode 解释）
    int64_t cval;       // IR_CONST 的立即数
    int cmp;            // CmpRel（IR_CMP / IR_BRCOND 用）
    const char* name;   // IR_CALL 函数名 / IR_LABEL 标签名
    int label;         // IR_BR / IR_BRCOND 跳转标签号
};

struct IrFunction {
    const char* name;
    List<IrInstr> code;
    int next_vreg;
    int next_label;
    int nparams;

    IrFunction() : name(0), next_vreg(0), next_label(0), nparams(0) {}
    int new_vreg();
    int new_label();
    int emit(const IrInstr& ins);
};

struct IrModule {
    List<IrFunction*> funcs;
    ~IrModule();
    IrFunction* new_func(const char* name);
};

// ---- AST -> IR 翻译器 ----
struct IrGen {
    CompContext& cc;
    IrModule& mod;
    IrFunction* cur;
    // 局部变量名 -> 栈槽 vreg（用 ALLOCA 分配的地址寄存器）
    List<const char*> var_names;
    List<int> var_slots;
    // 循环标签栈：break 跳到 break_lab，continue 跳到 cont_lab
    List<int> break_lab;
    List<int> cont_lab;

    IrGen(CompContext& c, IrModule& m) : cc(c), mod(m), cur(0) {}

    int gen(Node* n);                       // 表达式 -> vreg
    void gen_stmt(Node* s);
    void gen_func(FuncNode* f);
    int lookup_var(const char* name, int namelen);
};

// 入口：整棵 ProgramNode 翻译成 IrModule
IrModule* ir_gen_program(CompContext& cc, ProgramNode* prog);

// 打印 IR 到字符串
void ir_dump(String& out, IrModule* mod);

// 校验一个函数的 IR：检查目标 vreg 是否单调定义、跳转标签是否存在。
// 返回错误数（0 = 合法）。
int ir_validate_func(IrFunction* f);

// 活跃分析：统计每条指令定义/使用的 vreg，并返回函数中被定义过的 vreg 总数。
// live_count[i] 为指令 i 的后活跃寄存器数（近似，用于教学演示）。
int ir_liveness(IrFunction* f, List<int>& live_out);

// 死代码消除：删除结果从未被使用且无副作用的计算指令。返回删除条数。
int ir_dce(IrFunction* f);

// 语义分析：收集函数内声明的变量名，检查变量引用是否都已声明。
// 把诊断写入 report，返回错误数（0 = 通过）。
int sem_analyze(Node* root, String& report);

// 常量折叠优化：扫描函数指令，把 (const a) op (const b) 折叠成单个 const。
// 返回被折叠掉的指令条数。
int ir_constant_fold(IrFunction* f);

// 计算每个被定义 vreg 的活跃区间 [定义行, 最后一次使用行]，写入 out 文本。
// 返回区间条数。
int ir_live_intervals(IrFunction* f, String& out);

// 基本块切分：在标签/分支处把指令流切成基本块，打印块号与指令数。
// 返回基本块数量。
int ir_basic_blocks(IrFunction* f, String& out);

// 控制流图：打印块间跳转边（from -> to），返回边数。
int ir_cfg_edges(IrFunction* f, String& out);

// IR 自测试：返回失败数
int ir_self_test();

} // namespace compiler
} // namespace nefu
