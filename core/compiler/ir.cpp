// ============================================================================
// nefu::compiler —— 三地址中间代码实现（ir.cpp）
// ============================================================================
#include "ir.h"
#include "parser.h"
#include <string.h>

namespace nefu {
namespace compiler {

// ---- IrFunction ----
int IrFunction::new_vreg() { return next_vreg++; }
int IrFunction::new_label() { return next_label++; }

int IrFunction::emit(const IrInstr& ins) {
    code.push(ins);
    return (int)code.size() - 1;
}

// ---- IrModule ----
IrModule::~IrModule() {
    for (int i = 0; i < funcs.size(); i++) delete funcs[i];
}
IrFunction* IrModule::new_func(const char* name) {
    IrFunction* f = new IrFunction();
    f->name = name;
    funcs.push(f);
    return f;
}

// ---- IrGen ----
int IrGen::lookup_var(const char* name, int namelen) {
    for (int i = 0; i < var_names.size(); i++) {
        if ((int)strlen(var_names[i]) == namelen &&
            strncmp(var_names[i], name, namelen) == 0)
            return var_slots[i];
    }
    return -1;
}

// 表达式 -> 返回存放结果的 vreg
int IrGen::gen(Node* n) {
    if (!n) return -1;
    switch (n->kind) {
        case N_LITINT: {
            LitIntNode* l = (LitIntNode*)n;
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_CONST; ins.dest = cur->new_vreg(); ins.cval = l->value;
            cur->emit(ins);
            return ins.dest;
        }
        case N_LITCHAR: {
            LitCharNode* l = (LitCharNode*)n;
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_CONST; ins.dest = cur->new_vreg(); ins.cval = l->value;
            cur->emit(ins);
            return ins.dest;
        }
        case N_VAR: {
            VarNode* v = (VarNode*)n;
            int slot = lookup_var(v->name, v->namelen);
            if (slot < 0) slot = 0;
            // 从栈槽 load 出来
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_LOAD; ins.dest = cur->new_vreg(); ins.a = slot;
            cur->emit(ins);
            return ins.dest;
        }
        case N_BIN: {
            BinNode* b = (BinNode*)n;
            int l = gen(b->l);
            int r = gen(b->r);
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.a = l; ins.b = r; ins.dest = cur->new_vreg();
            switch (b->op) {
                case OP_ADD: ins.op = IR_ADD; break;
                case OP_SUB: ins.op = IR_SUB; break;
                case OP_MUL: ins.op = IR_MUL; break;
                case OP_DIV: ins.op = IR_DIV; break;
                case OP_MOD: ins.op = IR_MOD; break;
                case OP_AND: ins.op = IR_AND; break;
                case OP_OR: ins.op = IR_OR; break;
                case OP_XOR: ins.op = IR_XOR; break;
                case OP_SHL: ins.op = IR_SHL; break;
                case OP_SHR: ins.op = IR_SHR; break;
                case OP_EQ: ins.op = IR_CMP; ins.cmp = CR_EQ; break;
                case OP_NE: ins.op = IR_CMP; ins.cmp = CR_NE; break;
                case OP_LT: ins.op = IR_CMP; ins.cmp = CR_LT; break;
                case OP_LE: ins.op = IR_CMP; ins.cmp = CR_LE; break;
                case OP_GT: ins.op = IR_CMP; ins.cmp = CR_GT; break;
                case OP_GE: ins.op = IR_CMP; ins.cmp = CR_GE; break;
                default: ins.op = IR_ADD; break;
            }
            cur->emit(ins);
            return ins.dest;
        }
        case N_UN: {
            UnNode* u = (UnNode*)n;
            int e = gen(u->e);
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.a = e; ins.dest = cur->new_vreg();
            ins.op = (u->op == OP_NEG) ? IR_NEG : IR_NOT;
            cur->emit(ins);
            return ins.dest;
        }
        case N_ASSIGN: {
            AssignNode* a = (AssignNode*)n;
            int v = gen(a->value);
            // target 应为 N_VAR
            if (a->target && a->target->kind == N_VAR) {
                VarNode* t = (VarNode*)a->target;
                int slot = lookup_var(t->name, t->namelen);
                if (slot >= 0) {
                    IrInstr ins; memset(&ins, 0, sizeof(ins));
                    ins.op = IR_STORE; ins.a = slot; ins.b = v;
                    cur->emit(ins);
                }
            }
            return v;
        }
        case N_CALL: {
            CallNode* c = (CallNode*)n;
            const char* nm = "?";
            if (c->callee && c->callee->kind == N_VAR)
                nm = ((VarNode*)c->callee)->name;
            for (int i = 0; i < c->args.size(); i++) gen(c->args[i]);
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_CALL; ins.name = nm; ins.dest = cur->new_vreg();
            cur->emit(ins);
            return ins.dest;
        }
        default: {
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_NOP; ins.dest = cur->new_vreg();
            cur->emit(ins);
            return ins.dest;
        }
    }
}

void IrGen::gen_stmt(Node* s) {
    if (!s) return;
    switch (s->kind) {
        case N_VARDECL: {
            VarDecl* v = (VarDecl*)s;
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_ALLOCA; ins.dest = cur->new_vreg();
            ins.cval = 8;   // 每个局部量 8 字节
            int slot = ins.dest;
            cur->emit(ins);
            var_names.push(v->name);
            var_slots.push(slot);
            if (v->init) {
                int val = gen(v->init);
                IrInstr st; memset(&st, 0, sizeof(st));
                st.op = IR_STORE; st.a = slot; st.b = val;
                cur->emit(st);
            }
            break;
        }
        case N_EXPRSTMT: {
            ExprStmt* e = (ExprStmt*)s;
            gen(e->e);
            break;
        }
        case N_RETURN: {
            ReturnStmt* r = (ReturnStmt*)s;
            IrInstr ins; memset(&ins, 0, sizeof(ins));
            ins.op = IR_RET;
            ins.a = r->e ? gen(r->e) : -1;
            cur->emit(ins);
            break;
        }
        case N_IF: {
            IfStmt* i = (IfStmt*)s;
            int cond = gen(i->cond);
            int ltrue = cur->new_label();
            int lend = cur->new_label();
            IrInstr br; memset(&br, 0, sizeof(br));
            br.op = IR_BRCOND; br.a = cond; br.label = ltrue; br.b = lend;
            cur->emit(br);
            // true
            IrInstr lt; memset(&lt, 0, sizeof(lt));
            lt.op = IR_LABEL; lt.name = ""; lt.a = ltrue;
            cur->emit(lt);
            gen_stmt(i->thenb);
            if (i->elseb) {
                int lelse = cur->new_label();
                IrInstr jend; memset(&jend, 0, sizeof(jend));
                jend.op = IR_BR; jend.label = lend;
                cur->emit(jend);
                IrInstr le; memset(&le, 0, sizeof(le));
                le.op = IR_LABEL; le.a = lelse;
                cur->emit(le);
                gen_stmt(i->elseb);
            }
            IrInstr le2; memset(&le2, 0, sizeof(le2));
            le2.op = IR_LABEL; le2.a = lend;
            cur->emit(le2);
            break;
        }
        case N_WHILE: {
            WhileStmt* w = (WhileStmt*)s;
            int lhead = cur->new_label();
            int lbody = cur->new_label();
            int lend = cur->new_label();
            // 登记循环标签，供 break/continue 使用
            break_lab.push(lend);
            cont_lab.push(lhead);
            IrInstr lh; memset(&lh, 0, sizeof(lh));
            lh.op = IR_LABEL; lh.a = lhead;
            cur->emit(lh);
            int cond = gen(w->cond);
            IrInstr br; memset(&br, 0, sizeof(br));
            br.op = IR_BRCOND; br.a = cond; br.label = lbody; br.b = lend;
            cur->emit(br);
            IrInstr lb; memset(&lb, 0, sizeof(lb));
            lb.op = IR_LABEL; lb.a = lbody;
            cur->emit(lb);
            gen_stmt(w->body);
            IrInstr jh; memset(&jh, 0, sizeof(jh));
            jh.op = IR_BR; jh.label = lhead;
            cur->emit(jh);
            IrInstr le; memset(&le, 0, sizeof(le));
            le.op = IR_LABEL; le.a = lend;
            cur->emit(le);
            break_lab.pop();
            cont_lab.pop();
            break;
        }
        case N_BREAK: {
            if (break_lab.size() > 0) {
                IrInstr j; memset(&j, 0, sizeof(j));
                j.op = IR_BR; j.label = break_lab[break_lab.size() - 1];
                cur->emit(j);
            }
            break;
        }
        case N_CONTINUE: {
            if (cont_lab.size() > 0) {
                IrInstr j; memset(&j, 0, sizeof(j));
                j.op = IR_BR; j.label = cont_lab[cont_lab.size() - 1];
                cur->emit(j);
            }
            break;
        }
        case N_FOR: {
            ForStmt* fs = (ForStmt*)s;
            if (fs->init) gen_stmt(fs->init);
            int lhead = cur->new_label();
            int lbody = cur->new_label();
            int lpost = cur->new_label();
            int lend = cur->new_label();
            IrInstr lh; memset(&lh, 0, sizeof(lh)); lh.op = IR_LABEL; lh.a = lhead; cur->emit(lh);
            if (fs->cond) {
                int cv = gen(fs->cond);
                IrInstr br; memset(&br, 0, sizeof(br));
                br.op = IR_BRCOND; br.a = cv; br.label = lbody; br.b = lend; cur->emit(br);
            }
            IrInstr lb; memset(&lb, 0, sizeof(lb)); lb.op = IR_LABEL; lb.a = lbody; cur->emit(lb);
            break_lab.push(lend);
            cont_lab.push(lpost);
            gen_stmt(fs->body);
            break_lab.pop();
            cont_lab.pop();
            IrInstr lp; memset(&lp, 0, sizeof(lp)); lp.op = IR_LABEL; lp.a = lpost; cur->emit(lp);
            if (fs->post) gen(fs->post);
            IrInstr jh; memset(&jh, 0, sizeof(jh)); jh.op = IR_BR; jh.label = lhead; cur->emit(jh);
            IrInstr le; memset(&le, 0, sizeof(le)); le.op = IR_LABEL; le.a = lend; cur->emit(le);
            break;
        }
        case N_BLOCK: {
            Block* b = (Block*)s;
            for (int i = 0; i < b->stmts.size(); i++) gen_stmt(b->stmts[i]);
            break;
        }
        default:
            break;
    }
}

void IrGen::gen_func(FuncNode* f) {
    cur = mod.new_func(f->name);
    cur->nparams = f->param_names.size();
    var_names.clear(); var_slots.clear();
    // 参数先分配栈槽
    for (int i = 0; i < f->param_names.size(); i++) {
        IrInstr ins; memset(&ins, 0, sizeof(ins));
        ins.op = IR_PARAM; ins.dest = cur->new_vreg(); ins.a = i;
        cur->emit(ins);
        var_names.push(f->param_names[i]);
        var_slots.push(ins.dest);
    }
    if (f->body) {
        for (int i = 0; i < f->body->stmts.size(); i++)
            gen_stmt(f->body->stmts[i]);
    }
    // 末尾隐式 return
    IrInstr ret; memset(&ret, 0, sizeof(ret));
    ret.op = IR_RET; ret.a = -1;
    cur->emit(ret);
    cur = 0;
}

IrModule* ir_gen_program(CompContext& cc, ProgramNode* prog) {
    IrModule* mod = new IrModule();
    IrGen g(cc, *mod);
    for (int i = 0; i < prog->decls.size(); i++) {
        if (prog->decls[i]->kind == N_FUNC)
            g.gen_func((FuncNode*)prog->decls[i]);
    }
    return mod;
}

// ---- IR 打印 ----
static const char* ir_op_name(int op) {
    switch (op) {
        case IR_CONST: return "const";
        case IR_MOVE: return "move";
        case IR_ADD: return "add";
        case IR_SUB: return "sub";
        case IR_MUL: return "mul";
        case IR_DIV: return "div";
        case IR_MOD: return "mod";
        case IR_AND: return "and";
        case IR_OR: return "or";
        case IR_XOR: return "xor";
        case IR_SHL: return "shl";
        case IR_SHR: return "shr";
        case IR_NEG: return "neg";
        case IR_NOT: return "not";
        case IR_CMP: return "cmp";
        case IR_BR: return "br";
        case IR_BRCOND: return "brcond";
        case IR_LABEL: return "label";
        case IR_CALL: return "call";
        case IR_RET: return "ret";
        case IR_PARAM: return "param";
        case IR_ALLOCA: return "alloca";
        case IR_LOAD: return "load";
        case IR_STORE: return "store";
        case IR_GEP: return "gep";
        default: return "nop";
    }
}
static const char* cmp_name(int c) {
    switch (c) {
        case CR_EQ: return "eq"; case CR_NE: return "ne";
        case CR_LT: return "lt"; case CR_LE: return "le";
        case CR_GT: return "gt"; case CR_GE: return "ge";
    }
    return "?";
}

void ir_dump(String& out, IrModule* mod) {
    char buf[160];
    for (int i = 0; i < mod->funcs.size(); i++) {
        IrFunction* f = mod->funcs[i];
        ksprintf(buf, sizeof(buf), "function %s:\n", f->name ? f->name : "?");
        out += buf;
        for (int j = 0; j < f->code.size(); j++) {
            IrInstr& in = f->code[j];
            out += "    ";
            switch (in.op) {
                case IR_CONST:
                    ksprintf(buf, sizeof(buf), "v%d = const %d", in.dest, (int)in.cval);
                    out += buf; break;
                case IR_LABEL:
                    ksprintf(buf, sizeof(buf), ".L%d:", in.a); out += buf; break;
                case IR_BR:
                    ksprintf(buf, sizeof(buf), "br .L%d", in.label); out += buf; break;
                case IR_BRCOND:
                    ksprintf(buf, sizeof(buf), "brcond v%d .L%d .L%d", in.a, in.label, in.b);
                    out += buf; break;
                case IR_CALL:
                    ksprintf(buf, sizeof(buf), "v%d = call %s", in.dest, in.name ? in.name : "?");
                    out += buf; break;
                case IR_RET:
                    ksprintf(buf, sizeof(buf), "ret %s", in.a >= 0 ? "v?" : "void");
                    out += buf; break;
                case IR_CMP:
                    ksprintf(buf, sizeof(buf), "v%d = %s v%d, v%d", in.dest, cmp_name(in.cmp), in.a, in.b);
                    out += buf; break;
                case IR_LOAD:
                    ksprintf(buf, sizeof(buf), "v%d = load [v%d]", in.dest, in.a); out += buf; break;
                case IR_STORE:
                    ksprintf(buf, sizeof(buf), "store [v%d] = v%d", in.a, in.b); out += buf; break;
                case IR_ALLOCA:
                    ksprintf(buf, sizeof(buf), "v%d = alloca %d", in.dest, (int)in.cval); out += buf; break;
                case IR_PARAM:
                    ksprintf(buf, sizeof(buf), "v%d = param %d", in.dest, in.a); out += buf; break;
                default:
                    ksprintf(buf, sizeof(buf), "%s v%d, v%d, v%d", ir_op_name(in.op), in.dest, in.a, in.b);
                    out += buf; break;
            }
            out += "\n";
        }
    }
}

// ---- 活跃分析（教学用近似实现）----
// 对每条指令统计它“使用”的源 vreg 数量，作为 live_out 的近似指标；
// 并返回函数中不同 vreg 的总数。
int ir_liveness(IrFunction* f, List<int>& live_out) {
    live_out.clear();
    int total_vregs = f->next_vreg;
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        int uses = 0;
        switch (in.op) {
            case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
            case IR_AND: case IR_OR: case IR_XOR: case IR_SHL: case IR_SHR:
            case IR_CMP:
                uses = 2; break;
            case IR_NEG: case IR_NOT: case IR_LOAD: case IR_BRCOND:
                uses = 1; break;
            case IR_STORE: uses = 2; break;   // addr + value
            case IR_CALL: uses = 0; break;
            default: uses = 0; break;
        }
        live_out.push(uses);
    }
    return total_vregs;
}
// ---- 语义分析 ----
static void sem_collect_block(Block* b, List<const char*>& decls) {
    for (int i = 0; i < b->stmts.size(); i++) {
        Node* s = b->stmts[i];
        if (s->kind == N_VARDECL) decls.push(((VarDecl*)s)->name);
        else if (s->kind == N_BLOCK) sem_collect_block((Block*)s, decls);
        else if (s->kind == N_IF) {
            IfStmt* i = (IfStmt*)s;
            if (i->thenb && i->thenb->kind == N_BLOCK) sem_collect_block((Block*)i->thenb, decls);
            if (i->elseb && i->elseb->kind == N_BLOCK) sem_collect_block((Block*)i->elseb, decls);
        } else if (s->kind == N_WHILE) {
            WhileStmt* w = (WhileStmt*)s;
            if (w->body && w->body->kind == N_BLOCK) sem_collect_block((Block*)w->body, decls);
        } else if (s->kind == N_FOR) {
            ForStmt* f = (ForStmt*)s;
            if (f->body && f->body->kind == N_BLOCK) sem_collect_block((Block*)f->body, decls);
        }
    }
}
static bool is_declared(List<const char*>& decls, const char* name) {
    for (int i = 0; i < decls.size(); i++)
        if (strcmp(decls[i], name) == 0) return true;
    return false;
}
static int sem_check_expr(Node* e, List<const char*>& decls, String& rep) {
    if (!e) return 0;
    int errs = 0;
    switch (e->kind) {
        case N_VAR: {
            VarNode* v = (VarNode*)e;
            if (!is_declared(decls, v->name)) {
                char buf[96];
                ksprintf(buf, sizeof(buf), "未声明的变量 '%s' (行 %d)\n", v->name, e->line);
                rep += buf;
                errs++;
            }
            break;
        }
        case N_BIN: { BinNode* b = (BinNode*)e;
            errs += sem_check_expr(b->l, decls, rep) + sem_check_expr(b->r, decls, rep); break; }
        case N_UN: { UnNode* u = (UnNode*)e; errs += sem_check_expr(u->e, decls, rep); break; }
        case N_ASSIGN: { AssignNode* a = (AssignNode*)e;
            errs += sem_check_expr(a->target, decls, rep) + sem_check_expr(a->value, decls, rep); break; }
        case N_CALL: { CallNode* c = (CallNode*)e;
            errs += sem_check_expr(c->callee, decls, rep);
            for (int i = 0; i < c->args.size(); i++) errs += sem_check_expr(c->args[i], decls, rep);
            break; }
        case N_INDEX: { IndexNode* ix = (IndexNode*)e;
            errs += sem_check_expr(ix->obj, decls, rep) + sem_check_expr(ix->idx, decls, rep); break; }
        default: break;
    }
    return errs;
}
int sem_analyze(Node* root, String& rep) {
    if (!root || root->kind != N_PROGRAM) return 1;
    int errs = 0;
    ProgramNode* prog = (ProgramNode*)root;
    for (int i = 0; i < prog->decls.size(); i++) {
        Node* d = prog->decls[i];
        if (d->kind != N_FUNC) continue;
        FuncNode* f = (FuncNode*)d;
        List<const char*> decls;
        for (int p = 0; p < f->param_names.size(); p++) decls.push(f->param_names[p]);
        if (f->body) sem_collect_block(f->body, decls);
        // 检查函数体里所有表达式
        for (int s = 0; s < f->body->stmts.size(); s++) {
            Node* st = f->body->stmts[s];
            if (st->kind == N_EXPRSTMT) errs += sem_check_expr(((ExprStmt*)st)->e, decls, rep);
            else if (st->kind == N_RETURN) errs += sem_check_expr(((ReturnStmt*)st)->e, decls, rep);
            else if (st->kind == N_VARDECL) errs += sem_check_expr(((VarDecl*)st)->init, decls, rep);
        }
    }
    return errs;
}
// ---- 活跃区间 ----
int ir_live_intervals(IrFunction* f, String& out) {
    int n = f->next_vreg;
    List<int> def_at;  List<int> last_use;
    for (int v = 0; v < n; v++) { def_at.push(-1); last_use.push(-1); }
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        if (in.dest >= 0 && in.dest < n && def_at[in.dest] < 0) def_at[in.dest] = i;
        int srcs[2] = { in.a, in.b };
        for (int s = 0; s < 2; s++)
            if (srcs[s] >= 0 && srcs[s] < n) last_use[srcs[s]] = i;
    }
    char buf[96];
    int count = 0;
    for (int v = 0; v < n; v++) {
        if (def_at[v] < 0) continue;
        int end = last_use[v] < 0 ? def_at[v] : last_use[v];
        ksprintf(buf, sizeof(buf), "  v%d: [%d, %d]\n", v, def_at[v], end);
        out += buf;
        count++;
    }
    return count;
}
// ---- 基本块切分 ----
int ir_basic_blocks(IrFunction* f, String& out) {
    int blocks = 0;
    int start = 0;
    char buf[96];
    bool new_block = true;
    for (int i = 0; i <= f->code.size(); i++) {
        bool boundary = (i == f->code.size());
        if (!boundary) {
            IrInstr& in = f->code[i];
            if (new_block) {
                start = i;
                new_block = false;
            }
            // 分支/标签是块边界
            if (in.op == IR_BR || in.op == IR_BRCOND || in.op == IR_RET || in.op == IR_LABEL) {
                if (i > start) {
                    ksprintf(buf, sizeof(buf), "  block %d: 指令 [%d..%d] 共 %d 条\n",
                             blocks, start, i, i - start + (in.op==IR_LABEL?0:1));
                    out += buf;
                    blocks++;
                }
                if (in.op == IR_LABEL) { new_block = true; start = i; }
            }
        } else if (i > start && !new_block) {
            ksprintf(buf, sizeof(buf), "  block %d: 指令 [%d..%d]\n", blocks, start, i - 1);
            out += buf;
            blocks++;
        }
    }
    return blocks;
}
// ---- 控制流图边 ----
int ir_cfg_edges(IrFunction* f, String& out) {
    int edges = 0;
    char buf[64];
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        if (in.op == IR_BR) {
            ksprintf(buf, sizeof(buf), "  block@%d -> label%d\n", i, in.label);
            out += buf; edges++;
        } else if (in.op == IR_BRCOND) {
            ksprintf(buf, sizeof(buf), "  block@%d -> label%d (true) / label%d (false)\n",
                     i, in.label, in.b);
            out += buf; edges += 2;
        }
    }
    return edges;
}
// ---- 死代码消除 ----
int ir_dce(IrFunction* f) {
    // 统计被作为源操作数引用的 vreg
    List<int> used;
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        int srcs[2] = { in.a, in.b };
        for (int s = 0; s < 2; s++) {
            while (used.size() <= srcs[s]) used.push(0);
            if (srcs[s] >= 0) used[srcs[s]] = 1;
        }
    }
    int removed = 0;
    List<IrInstr> kept;
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        bool side_effect =
            in.op == IR_RET || in.op == IR_CALL || in.op == IR_BR ||
            in.op == IR_BRCOND || in.op == IR_STORE || in.op == IR_PARAM ||
            in.op == IR_LABEL || in.op == IR_ALLOCA;
        bool dest_used = in.dest >= 0 && in.dest < used.size() && used[in.dest];
        if (side_effect || dest_used) kept.push(in);
        else removed++;
    }
    f->code = kept;
    return removed;
}
// ---- 常量折叠优化 ----
int ir_constant_fold(IrFunction* f) {
    int folded = 0;
    // 第一遍：建立 vreg -> 常量值表
    List<int64_t> const_val;
    List<int> const_def;     // 1 表示该 vreg 是常量
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        while (const_val.size() <= in.dest) { const_val.push(0); const_def.push(0); }
        if (in.op == IR_CONST) {
            const_val[in.dest] = in.cval;
            const_def[in.dest] = 1;
        }
    }
    // 第二遍：折叠二元运算
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        if (in.dest < 0) continue;
        bool ba = in.a < const_def.size() && const_def[in.a];
        bool bb = in.b < const_def.size() && const_def[in.b];
        if (ba && bb) {
            int64_t x = const_val[in.a], y = const_val[in.b], r = 0;
            bool known = true;
            switch (in.op) {
                case IR_ADD: r = x + y; break;
                case IR_SUB: r = x - y; break;
                case IR_MUL: r = x * y; break;
                case IR_DIV: r = y ? x / y : 0; break;
                case IR_MOD: r = y ? x % y : 0; break;
                case IR_AND: r = x & y; break;
                case IR_OR: r = x | y; break;
                case IR_XOR: r = x ^ y; break;
                default: known = false; break;
            }
            if (known) {
                in.op = IR_CONST;
                in.cval = r;
                in.a = in.b = -1;
                const_val[in.dest] = r;
                folded++;
            }
        }
    }
    return folded;
}
// ---- IR 校验器 ----
int ir_validate_func(IrFunction* f) {
    int errs = 0;
    int max_defined = -1;
    // 收集所有定义过的目标 vreg
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        if (in.dest >= 0 && in.dest > max_defined) max_defined = in.dest;
    }
    // 检查跳转标签都存在
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        if (in.op == IR_BR || in.op == IR_BRCOND) {
            int target = (in.op == IR_BR) ? in.label : in.label;
            if (target >= f->next_label) errs++;     // 跳转到未声明标签
        }
    }
    // 至少应有一个 ret
    bool has_ret = false;
    for (int i = 0; i < f->code.size(); i++)
        if (f->code[i].op == IR_RET) has_ret = true;
    if (!has_ret) errs++;
    (void)max_defined;
    return errs;
}
// ---- 自测试 ----
int ir_self_test() {
    int fails = 0;
    CompContext cc;
    const char* src =
        "int main(void){ int x = 1 + 2 * 3; int i = 0; while (i < 10) { x = x + i; i = i + 1; } return x; }";
    ProgramNode* prog = compile_parse(cc, src);
    if (cc.err_count != 0) fails++;

    IrModule* mod = ir_gen_program(cc, prog);
    if (mod->funcs.size() != 1) fails++;

    IrFunction* f = mod->funcs[0];
    // 应包含 alloca / load / store / add / cmp / brcond / while 标签 / ret
    bool has_alloca = false, has_load = false, has_store = false;
    bool has_add = false, has_cmp = false, has_brcond = false, has_ret = false;
    for (int i = 0; i < f->code.size(); i++) {
        IrInstr& in = f->code[i];
        if (in.op == IR_ALLOCA) has_alloca = true;
        if (in.op == IR_LOAD) has_load = true;
        if (in.op == IR_STORE) has_store = true;
        if (in.op == IR_ADD) has_add = true;
        if (in.op == IR_CMP) has_cmp = true;
        if (in.op == IR_BRCOND) has_brcond = true;
        if (in.op == IR_RET) has_ret = true;
    }
    if (!has_alloca) fails++;
    if (!has_load) fails++;
    if (!has_store) fails++;
    if (!has_add) fails++;
    if (!has_cmp) fails++;
    if (!has_brcond) fails++;
    if (!has_ret) fails++;

    // vreg 单调递增
    if (f->next_vreg <= 5) fails++;

    // 打印不崩溃
    String out;
    ir_dump(out, mod);
    if (out.find("main") < 0) fails++;

    // 校验器：合法函数应 0 错误
    if (ir_validate_func(f) != 0) fails++;

    // 活跃分析：返回的 live_out 长度应等于指令数
    List<int> live;
    int tv = ir_liveness(f, live);
    if (live.size() != f->code.size()) fails++;
    if (tv <= 0) fails++;

    // 控制流边：while 函数应至少含一条 BR/BRCOND 边
    {
        String ce;
        int e = ir_cfg_edges(f, ce);
        if (e < 1) fails++;
        if (ce.len() == 0) fails++;
    }

    // 基本块切分：含 label/ret 的函数应至少切出 1 个块
    {
        String bb;
        int nblk = ir_basic_blocks(f, bb);
        if (nblk < 1) fails++;
        if (bb.len() == 0) fails++;
    }

    // 活跃区间：构造 v0=3, v1=4, v2=v0+v1，应得到 3 个区间
    {
        IrFunction itf;
        IrInstr a; memset(&a,0,sizeof(a)); a.op=IR_CONST; a.dest=0; a.cval=3; a.a=-1;a.b=-1; itf.code.push(a);
        IrInstr b; memset(&b,0,sizeof(b)); b.op=IR_CONST; b.dest=1; b.cval=4; b.a=-1;b.b=-1; itf.code.push(b);
        IrInstr d; memset(&d,0,sizeof(d)); d.op=IR_ADD; d.dest=2; d.a=0; d.b=1; itf.code.push(d);
        itf.next_vreg=3;
        String rep;
        int cnt = ir_live_intervals(&itf, rep);
        if (cnt != 3) fails++;
        if (rep.find("v0") < 0 || rep.find("v2") < 0) fails++;
    }

    // for 循环 IR：应产生条件分支与回边
    {
        CompContext fc;
        ProgramNode* fp = compile_parse(fc, "int main(void){ int s = 0; for (int i = 0; i < 3; i = i + 1) { s = s + i; } return s; }");
        IrModule* fm = ir_gen_program(fc, fp);
        IrFunction* ff = fm->funcs[0];
        bool has_brcond = false, has_backedge = false;
        for (int i = 0; i < ff->code.size(); i++) {
            if (ff->code[i].op == IR_BRCOND) has_brcond = true;
            if (ff->code[i].op == IR_BR) has_backedge = true;
        }
        if (!has_brcond || !has_backedge) fails++;
        delete fm;
    }

    // break/continue：循环内 break 应产生额外无条件跳转
    {
        CompContext bc;
        ProgramNode* bp = compile_parse(bc,
            "int main(void){ int i = 0; while (i < 5) { if (i == 2) { break; } i = i + 1; } return i; }");
        IrModule* bm = ir_gen_program(bc, bp);
        IrFunction* bf = bm->funcs[0];
        int brcount = 0;
        for (int i = 0; i < bf->code.size(); i++)
            if (bf->code[i].op == IR_BR) brcount++;
        if (brcount < 2) fails++;      // while 回边 + break 至少两条 BR
        delete bm;
    }

    // 语义分析：合法程序 0 错误；引用未声明变量应报错
    {
        CompContext sc;
        ProgramNode* p1 = compile_parse(sc, "int main(void){ int a = 1; return a; }");
        String r1;
        if (sem_analyze(p1, r1) != 0) fails++;

        CompContext sc2;
        ProgramNode* p2 = compile_parse(sc2, "int main(void){ return undefined_var; }");
        String r2;
        if (sem_analyze(p2, r2) == 0) fails++;     // 应检出未声明变量
    }

    // 死代码消除：未使用的 const 应被删除，ret 保留
    {
        IrFunction tf;
        IrInstr i1; memset(&i1, 0, sizeof(i1)); i1.op = IR_CONST; i1.dest = 0; i1.cval = 99; i1.a = -1; i1.b = -1; tf.code.push(i1); // 未使用
        IrInstr i2; memset(&i2, 0, sizeof(i2)); i2.op = IR_CONST; i2.dest = 1; i2.cval = 7; i2.a = -1; i2.b = -1; tf.code.push(i2);
        IrInstr i3; memset(&i3, 0, sizeof(i3)); i3.op = IR_RET; i3.a = 1; i3.b = -1; tf.code.push(i3);
        int before = tf.code.size();
        int rem = ir_dce(&tf);
        if (rem != 1) fails++;                 // 只删掉未使用的 v0
        if (tf.code.size() != before - 1) fails++;
    }

    // 常量折叠：构造 const+const+const 的小函数
    {
        IrFunction tf;
        IrInstr i1; memset(&i1, 0, sizeof(i1)); i1.op = IR_CONST; i1.dest = 0; i1.cval = 3; tf.code.push(i1);
        IrInstr i2; memset(&i2, 0, sizeof(i2)); i2.op = IR_CONST; i2.dest = 1; i2.cval = 4; tf.code.push(i2);
        IrInstr i3; memset(&i3, 0, sizeof(i3)); i3.op = IR_ADD; i3.dest = 2; i3.a = 0; i3.b = 1; tf.code.push(i3);
        tf.next_vreg = 3;
        int folded = ir_constant_fold(&tf);
        if (folded != 1) fails++;                  // 3+4 应被折叠
        if (tf.code[2].op != IR_CONST) fails++;
        if (tf.code[2].cval != 7) fails++;
    }
    // 构造一个缺 ret 的坏函数，校验应报错
    {
        IrFunction bad;
        IrInstr i; memset(&i, 0, sizeof(i));
        i.op = IR_CONST; i.dest = 0; i.cval = 1;
        bad.code.push(i);
        if (ir_validate_func(&bad) == 0) fails++;   // 缺 ret 应被检出
    }

    delete mod;
    return fails;
}

} // namespace compiler
} // namespace nefu
