// scene.cpp —— 场景管理纯逻辑：节点树遍历、世界矩阵、自测
#include "scene.h"


namespace nefu {
namespace gameengine {

// 递归查找子节点（按名字，深度优先）
SceneNode* SceneNode::find(const char* n) const {
    if (!n) return 0;
    // 简单字符串比较（避免依赖 String）
    for (int i = 0; i < children.size(); i++) {
        SceneNode* c = children[i];
        if (c->name) {
            const char* a = c->name; const char* b = n;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == 0 && *b == 0) return c;
        }
        SceneNode* r = c->find(n);
        if (r) return r;
    }
    return 0;
}

// 世界矩阵：沿父链 T*R*S 累乘
Mat3 SceneNode::world_matrix() const {
    Mat3 local = transform.to_matrix();
    if (!parent) return local;
    return Mat3::mul(parent->world_matrix(), local);
}

// 递归更新
void SceneNode::update(int dt_ms) {
    if (!active) return;
    for (int i = 0; i < components.size(); i++) {
        components[i]->update(*this, dt_ms);
    }
    for (int i = 0; i < children.size(); i++) {
        children[i]->update(dt_ms);
    }
}

// 递归节点计数
int SceneNode::deep_count() const {
    int n = 1;
    for (int i = 0; i < children.size(); i++) n += children[i]->deep_count();
    return n;
}

// ============================================================================
//  自测
// ============================================================================
// 一个计数组件：记录 update 被调用次数
struct CountComp : Component {
    int ticks;
    CountComp() : ticks(0) {}
    virtual void update(SceneNode&, int) override { ticks++; }
    virtual const char* type_name() const override { return "CountComp"; }
};

int scene_self_test() {
    int fails = 0;

    // 建场景：root -> player -> gun
    Scene s("level1");
    SceneNode* player = s.root->add_child(new SceneNode());
    player->name = "player";
    player->transform.position = Vec2(fx::itofix(100), fx::itofix(50));

    SceneNode* gun = player->add_child(new SceneNode());
    gun->name = "gun";
    gun->transform.position = Vec2(fx::itofix(10), 0);   // 相对 player

    SceneNode* enemy = s.root->add_child(new SceneNode());
    enemy->name = "enemy";
    enemy->transform.position = Vec2(fx::itofix(200), fx::itofix(50));

    // 节点计数：root + player + gun + enemy = 4
    if (s.node_count() != 4) fails++;

    // 按名字查找
    if (s.root->find("gun") != gun) fails++;
    if (s.root->find("player") != player) fails++;
    if (s.root->find("nonexist") != 0) fails++;

    // 世界坐标：gun 的世界位置 = player(100,50) + gun(10,0) = (110,50)
    Vec2 wp = gun->world_position();
    if (fx::fixtoi(wp.x) != 110 || fx::fixtoi(wp.y) != 50) fails++;

    // 父子变换：player 移动后 gun 跟着动
    player->transform.position = Vec2(fx::itofix(200), fx::itofix(80));
    wp = gun->world_position();
    if (fx::fixtoi(wp.x) != 210 || fx::fixtoi(wp.y) != 80) fails++;

    // 组件更新
    CountComp* cc = new CountComp();
    player->add_component(cc);
    s.update(16);
    s.update(16);
    s.update(16);
    if (cc->ticks != 3) fails++;

    // 暂停场景后不再更新
    s.paused = true;
    s.update(16);
    if (cc->ticks != 3) fails++;
    s.paused = false;

    // SceneManager 场景切换
    SceneManager mgr;
    Scene* menu = new Scene("menu");
    Scene* game = new Scene("game");
    mgr.switch_to(menu);
    if (mgr.current != menu) fails++;
    mgr.push(game);
    if (mgr.current != game) fails++;
    // 弹回 menu
    Scene* popped = mgr.pop();
    if (popped != game) fails++;
    if (mgr.current != menu) fails++;
    delete popped;

    // pause/resume
    mgr.pause_current();
    if (!mgr.current->paused) fails++;
    mgr.resume_current();
    if (mgr.current->paused) fails++;


    // --- Tween ---
    TweenSystem ts;
    Tween& tw = ts.add(0, nefu::fx::itofix(100), nefu::fx::itofix(1000), EASE_LINEAR);
    if (ts.running_count() != 1) fails++;
    // 250ms -> 25
    ts.update(250);
    { int v=fx::fixtoi(tw.value); if (v < 24 || v > 26) fails++; }
    // 再 750ms -> 完成
    ts.update(750);
    if (tw.state != TW_STOPPED) fails++;
    if (fx::fixtoi(tw.value) != 100) fails++;

    // pause/resume
    TweenSystem ts2;
    Tween& tw2 = ts2.add(0, nefu::fx::itofix(50), nefu::fx::itofix(1000), EASE_LINEAR);
    ts2.update(200);   // 20%
    tw2.pause();
    ts2.update(1000);  // 暂停时不应推进
    { int v=fx::fixtoi(tw2.value); if (v < 9 || v > 11) fails++; }
    tw2.resume();
    ts2.update(800);   // 剩余 80%
    if (tw2.state != TW_STOPPED) fails++;

    // --- Timer ---
    TimerSystem tsys;
    int id = tsys.schedule(fx::itofix(500), false);
    if (id <= 0) fails++;
    if (tsys.update(100) != -1) fails++;   // 未到点
    if (tsys.update(400) != id) fails++;   // 500ms 到
    if (tsys.update(100) != -1) fails++;   // 一次性，已完成

    // 重复定时器
    TimerSystem tsys2;
    int id2 = tsys2.schedule(fx::itofix(200), true);
    if (tsys2.update(200) != id2) fails++;
    if (tsys2.update(200) != id2) fails++;  // 再次触发

    // --- GameClock ---
    GameClock clk;
    clk.tick(0);
    int d1 = clk.tick(16);
    if (d1 != 16) fails++;
    if (!clk.should_step()) fails++;   // 16ms = fixed_step
    if (clk.should_step()) fails++;    // 已消费
    // 长时间无帧后 delta 被钳制
    int d2 = clk.tick(10000);
    if (d2 != 250) fails++;

    // --- SceneTransition ---
    SceneTransition st;
    st.start(TRANS_FADE, 1000);
    st.update(600);
    if (!st.half_done) fails++;
    st.update(600);
    if (st.active) fails++;
    return fails;
}

} // namespace gameengine
} // namespace nefu
