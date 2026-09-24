// ============================================================================
// nefu::minilang —— 运行环境 / 作用域链（实现）
// ============================================================================
#include "env.h"
#include <string.h>

namespace nefu {
namespace minilang {

Env* env_new(Env* parent) {
    Env* e = new Env();
    e->parent = parent;
    e->ref = 1;
    return e;
}

void env_retain(Env* e) {
    if (e) e->ref++;
}

void env_release(Env* e) {
    if (!e) return;
    e->ref--;
    if (e->ref > 0) return;
    // 释放所有绑定持有的值引用
    for (int i = 0; i < e->vars.size(); i++) {
        val_drop(e->vars[i].val);
    }
    // parent 不由本环境释放（所有权在闭包/全局），仅置空
    delete e;
}

void env_define(Env* e, const char* name, Value val) {
    if (!e) return;
    Binding b;
    b.name = String(name);
    b.val = val;        // 所有权转入
    e->vars.push(b);
}

Binding* env_lookup_local(Env* e, const char* name) {
    if (!e) return 0;
    for (int i = 0; i < e->vars.size(); i++) {
        if (strcmp(e->vars[i].name.c_str(), name) == 0) return &e->vars[i];
    }
    return 0;
}

Value* env_lookup(Env* e, const char* name) {
    for (Env* cur = e; cur; cur = cur->parent) {
        Binding* b = env_lookup_local(cur, name);
        if (b) return &b->val;
    }
    return 0;
}

bool env_assign(Env* e, const char* name, Value val) {
    for (Env* cur = e; cur; cur = cur->parent) {
        Binding* b = env_lookup_local(cur, name);
        if (b) {
            val_drop(b->val);     // 释放旧值
            b->val = val_dup(val); // 存入新值（dup）
            return true;
        }
    }
    return false;
}

int env_self_test() {
    int fails = 0;

    Env* g = env_new(0);
    env_define(g, "a", val_make_int(1));
    env_define(g, "b", val_make_int(2));

    Value* pa = env_lookup(g, "a");
    if (!pa || val_as_int(*pa) != 1) fails++;

    // 子作用域遮蔽
    Env* c = env_new(g);
    env_define(c, "a", val_make_int(10));
    Value* pa2 = env_lookup(c, "a");
    if (!pa2 || val_as_int(*pa2) != 10) fails++;
    // 父级 b 仍可达
    Value* pb = env_lookup(c, "b");
    if (!pb || val_as_int(*pb) != 2) fails++;

    // 沿链赋值：更新子作用域的 a
    if (!env_assign(c, "a", val_make_int(20))) fails++;
    Value* pa3 = env_lookup(c, "a");
    if (!pa3 || val_as_int(*pa3) != 20) fails++;
    // 父级 a 不变
    Value* pa4 = env_lookup(g, "a");
    if (!pa4 || val_as_int(*pa4) != 1) fails++;

    // 未定义赋值
    if (env_assign(c, "nope", val_make_int(1))) fails++;

    env_release(c);
    env_release(g);
    return fails;
}

} // namespace minilang
} // namespace nefu
