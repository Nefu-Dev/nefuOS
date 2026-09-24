// scene.h —— 场景管理：场景/节点/组件、父子变换、场景切换、暂停/恢复
//
// 纯逻辑，不依赖 gfxlib。节点树以 List 持有子节点指针。
#pragma once

#include <stdint.h>
#include "../klib/klib.h"
#include "ge_math.h"

namespace nefu {
namespace gameengine {

// 前置声明
struct SceneNode;
struct Scene;

// ============================================================================
//  Component —— 组件基类（ECS 的轻量版：节点挂组件）
//  用普通虚函数即可（虽然 -fno-rtti，但虚表本身不需要 RTTI）。
// ============================================================================
struct Component {
    virtual ~Component() {}
    // 每帧更新；返回 false 可阻止后续组件更新（这里简化为都调用）
    virtual void update(SceneNode& node, int dt_ms) { (void)node; (void)dt_ms; }
    virtual const char* type_name() const { return "Component"; }
};

// ============================================================================
//  SceneNode —— 场景节点：带变换的实体
// ============================================================================
struct SceneNode {
    Transform2D  transform;
    SceneNode*   parent;
    List<SceneNode*> children;
    List<Component*> components;
    const char*  name;
    bool         active;
    bool         visible;
    void*        userdata;
    int          id;

    SceneNode() : parent(0), name(0), active(true), visible(true),
                  userdata(0), id(0) {}

    ~SceneNode() {
        for (int i = 0; i < children.size(); i++) delete children[i];
        children.clear();
        for (int i = 0; i < components.size(); i++) delete components[i];
        components.clear();
    }

    // 添加子节点（取得所有权）
    SceneNode* add_child(SceneNode* child) {
        child->parent = this;
        children.push(child);
        return child;
    }

    // 挂组件（取得所有权）
    void add_component(Component* c) { components.push(c); }

    // 查找子节点（按名字，递归）
    SceneNode* find(const char* n) const;

    // 世界矩阵：沿父链累乘
    Mat3 world_matrix() const;

    // 世界位置
    Vec2 world_position() const { return world_matrix().transform_point(Vec2(0,0)); }

    // 每帧更新（递归子节点 + 组件）
    void update(int dt_ms);

    // 子节点计数（递归）
    int deep_count() const;
};

// ============================================================================
//  Scene —— 一个场景
// ============================================================================
struct Scene {
    const char* name;
    SceneNode*  root;
    bool        loaded;
    bool        paused;

    Scene(const char* n) : name(n), root(0), loaded(false), paused(false) {
        root = new SceneNode();
        root->name = "root";
    }
    ~Scene() { delete root; root = 0; }

    // 递归更新
    void update(int dt_ms) {
        if (paused || !root) return;
        root->update(dt_ms);
    }

    int node_count() const { return root ? root->deep_count() : 0; }
};

// ============================================================================
//  SceneManager —— 场景栈：切换/推入/弹出/暂停
// ============================================================================
struct SceneManager {
    List<Scene*> stack;
    Scene* current;

    SceneManager() : current(0) {}
    ~SceneManager() {
        for (int i = 0; i < stack.size(); i++) delete stack[i];
        stack.clear();
        current = 0;
    }

    // 切换：清空栈，压入新场景
    void switch_to(Scene* s) {
        for (int i = 0; i < stack.size(); i++) delete stack[i];
        stack.clear();
        stack.push(s);
        current = s;
    }

    // 压栈（保留上一个场景，如暂停菜单）
    void push(Scene* s) {
        stack.push(s);
        current = s;
    }

    // 弹栈（返回上一个场景）；返回被弹出的场景（调用者删除）
    Scene* pop() {
        if (stack.size() <= 1) return 0;
        Scene* top = stack.pop();
        current = stack[stack.size() - 1];
        return top;
    }

    void pause_current() { if (current) current->paused = true; }
    void resume_current() { if (current) current->paused = false; }

    void update(int dt_ms) { if (current) current->update(dt_ms); }
};

// ============================================================================
//  自测
// ============================================================================

// ============================================================================
//  Tween —— 补间动画系统：对 fix/Vec2 做定时缓动
// ============================================================================
enum TweenState {
    TW_STOPPED = 0,
    TW_RUNNING,
    TW_PAUSED,
};

struct Tween {
    fix      from, to;
    fix      elapsed;
    fix      duration_ms;
    EaseType ease;
    TweenState state;
    fix      value;

    Tween() : from(0), to(0), elapsed(0), duration_ms(0),
              ease(EASE_LINEAR), state(TW_STOPPED), value(0) {}

    void start(fix f, fix t, fix dur_ms, EaseType e) {
        from = f; to = t; elapsed = 0; duration_ms = dur_ms;
        ease = e; state = TW_RUNNING; value = f;
    }
    void pause()  { if (state == TW_RUNNING) state = TW_PAUSED; }
    void resume() { if (state == TW_PAUSED) state = TW_RUNNING; }
    void stop()   { state = TW_STOPPED; }

    // 返回 false 表示完成
    bool update(int dt_ms) {
        if (state != TW_RUNNING) return state == TW_RUNNING;
        elapsed += nefu::fx::itofix(dt_ms);
        fix t = nefu::fx::fx_div(elapsed, duration_ms);
        if (t >= nefu::fx::FX_ONE) {
            t = nefu::fx::FX_ONE;
            state = TW_STOPPED;
            value = to;
            return false;
        }
        fix k = easing(ease, t);
        value = from + nefu::fx::fx_mul(to - from, k);
        return true;
    }
};

// Tween 集合：批量推进
struct TweenSystem {
    List<Tween> tweens;
    void update(int dt_ms) {
        for (int i = 0; i < tweens.size(); i++) tweens[i].update(dt_ms);
    }
    Tween& add(fix from, fix to, fix dur_ms, EaseType e) {
        Tween t; t.start(from, to, dur_ms, e);
        tweens.push(t);
        return tweens[tweens.size() - 1];
    }
    int running_count() const {
        int n = 0;
        for (int i = 0; i < tweens.size(); i++)
            if (tweens[i].state == TW_RUNNING) n++;
        return n;
    }
};

// ============================================================================
//  Timer —— 一次性/重复定时器
// ============================================================================
struct Timer {
    fix      delay_ms;
    fix      elapsed;
    bool     repeating;
    bool     done;
    int      id;

    Timer() : delay_ms(0), elapsed(0), repeating(false), done(false), id(0) {}
};

const int GE_TIMER_MAX = 16;
struct TimerSystem {
    Timer timers[GE_TIMER_MAX];
    int   count;
    int   fired_id;       // 最近一次触发的 id（-1=无）

    TimerSystem() : count(0), fired_id(-1) {}

    int schedule(fix delay_ms, bool repeat) {
        if (count >= GE_TIMER_MAX) return -1;
        Timer& t = timers[count++];
        t.delay_ms = delay_ms;
        t.elapsed = 0;
        t.repeating = repeat;
        t.done = false;
        t.id = count;
        return t.id;
    }

    // 推进；返回本帧触发的 timer id（-1=无）
    int update(int dt_ms) {
        fired_id = -1;
        for (int i = 0; i < count; i++) {
            Timer& t = timers[i];
            if (t.done) continue;
            t.elapsed += nefu::fx::itofix(dt_ms);
            if (t.elapsed >= t.delay_ms) {
                fired_id = t.id;
                if (t.repeating) t.elapsed = 0;
                else t.done = true;
            }
        }
        return fired_id;
    }
};

// ============================================================================
//  GameClock —— 游戏时钟：固定步长累加器、FPS 统计
// ============================================================================
struct GameClock {
    int   last_ms;
    int   delta_ms;       // 上一帧耗时
    int   accumulator;    // 固定步长累加
    int   fixed_step;     // 固定步长（ms）
    int   frames;
    int   fps_accum;      // 秒级累加
    int   fps;
    bool  initialized;

    GameClock() : last_ms(0), delta_ms(0), accumulator(0),
                  fixed_step(16), frames(0), fps_accum(0), fps(0), initialized(false) {}

    // 喂入当前时间（platform_tick_ms），返回本帧 delta
    int tick(int now_ms) {
        if (!initialized) { last_ms = now_ms; delta_ms = 16; initialized = true; return 16; }
        delta_ms = now_ms - last_ms;
        if (delta_ms > 250) delta_ms = 250;   // 后台切换防跳变
        last_ms = now_ms;
        accumulator += delta_ms;
        frames++;
        fps_accum += delta_ms;
        if (fps_accum >= 1000) {
            fps = frames * 1000 / fps_accum;
            frames = 0;
            fps_accum = 0;
        }
        return delta_ms;
    }
    // 是否该跑一个固定步长
    bool should_step() {
        if (accumulator >= fixed_step) { accumulator -= fixed_step; return true; }
        return false;
    }
};

// ============================================================================
//  SceneTransition —— 场景切换过渡效果
// ============================================================================
enum TransitionType {
    TRANS_FADE = 0,
    TRANS_SLIDE_LEFT,
    TRANS_SLIDE_RIGHT,
};

struct SceneTransition {
    TransitionType type;
    int  duration_ms;
    int  elapsed;
    bool active;
    bool half_done;     // 中点（可在此切场景）

    SceneTransition() : type(TRANS_FADE), duration_ms(300),
                        elapsed(0), active(false), half_done(false) {}

    void start(TransitionType t, int ms) {
        type = t; duration_ms = ms; elapsed = 0; active = true; half_done = false;
    }
    // 返回 0..1 进度
    fix progress() const {
        if (!active) return 0;
        int p = (duration_ms > 0) ? elapsed * 65536 / duration_ms : 65536;
        if (p > 65536) p = 65536;
        return p;
    }
    bool update(int dt_ms) {
        if (!active) return false;
        elapsed += dt_ms;
        if (!half_done && elapsed >= duration_ms / 2) half_done = true;
        if (elapsed >= duration_ms) { active = false; return false; }
        return true;
    }
};

int scene_self_test();

} // namespace gameengine
} // namespace nefu
