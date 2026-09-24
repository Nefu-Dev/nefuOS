// gedemo.cpp —— 2D 游戏引擎演示窗口
//
// 演示：瓦片地图背景 + 物理刚体（左右移动的箱子，重力/碰撞地板）
//       + 粒子发射器（跟随箱子）+ UI（血条/按钮/菜单）+ 输入映射。
// 控制：
//   左/右 方向键  : 移动箱子
//   空格          : 跳跃
//   P             : 发射一波粒子
//   H             : 扣血（测试血条）
//   ESC           : 关闭
// 参考 algoviz.cpp 窗口应用模式。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../gameengine/gameengine_all.h"

using namespace nefu::gameengine;

namespace nefu {

namespace {

const int DEMO_W = 640, DEMO_H = 400;

// 演示状态
struct GeDemo {
    // 物理世界
    PhysicsWorld world;
    RigidBody*  ball;       // 玩家箱子
    RigidBody*  floor;     // 地板
    RigidBody*  box2;     // 掉落箱
    RigidBody*  plat;      // 平台

    // 粒子
    ParticleSystem psys;
    Emitter        emitter;
    uint32_t       rng;

    // 输入
    InputMapper    input;

    // UI
    UIHealthBar   hp;
    UIButton      btn;
    UIMenu        menu;

    Tilemap       tmap;
    Camera        cam;
    UINotification note;
    int tick_ms;
    int fps_accum;
    int fps_frames;
    int fps;
    bool          running;

    GeDemo() : rng(12345), tick_ms(0), fps_accum(0), fps_frames(0), fps(0), running(true) {
        // --- 物理：地板 + 箱子 ---
        floor = world.add_body(Shape::make_box(400, 20), BODY_STATIC);
        floor->position = Vec2(fx::itofix(320), fx::itofix(380));
        ball = world.add_body(Shape::make_box(16, 16), BODY_DYNAMIC);
        ball->position = Vec2(fx::itofix(320), fx::itofix(100));
        ball->restitution = fx::fxf(2, 10);   // 0.2

        // --- 粒子发射器：跟随箱子 ---
        emitter.shape = EMIT_CONE;
        emitter.angle = 0;                    // 朝右（屏幕 +x）
        emitter.spread = nefu::fx::FX_PI_2;   // 全向
        emitter.rate = nefu::fx::itofix(60);
        emitter.speed_min = nefu::fx::itofix(40);
        emitter.speed_max = nefu::fx::itofix(120);
        emitter.life_ms = nefu::fx::itofix(600);
        emitter.gravity = nefu::fx::itofix(100);
        emitter.size_start = nefu::fx::itofix(3);

        // --- UI ---
        hp.rect = UIRect(10, 10, 200, 14);
        hp.max = 100; hp.value = 80;
        btn.rect = UIRect(10, 34, 90, 26);
        btn.label = "Jump!";
        menu.x = DEMO_W - 130; menu.y = 10; menu.item_h = 20;
        menu.add_item("Resume");
        menu.add_item("Restart");
        menu.add_item("Quit");

        // 输入轴：左=KEY_LEFT, 右=KEY_RIGHT
        input.add_axis(KEY_LEFT, KEY_RIGHT);
        input.add_button(KEY_SPACE);

        box2 = world.add_body(Shape::make_box(12, 12), BODY_DYNAMIC);
        box2->position = Vec2(fx::itofix(150), fx::itofix(20));
        plat = world.add_body(Shape::make_box(80, 10), BODY_STATIC);
        plat->position = Vec2(fx::itofix(200), fx::itofix(300));
    }

    void step(int dt) {
        tick_ms += dt;
        fps_accum += dt; fps_frames++;
        if (fps_accum >= 1000) { fps = fps_frames; fps_frames = 0; fps_accum = 0; }
        input.update();
        input.state.end_frame();

        // 左右移动
        fix ax = input.axes[0].value;
        ball->velocity.x = nefu::fx::fx_mul(ax, nefu::fx::itofix(180));
        // 跳跃
        if (input.buttons[0].pressed) {
            ball->velocity.y = nefu::fx::itofix(-260);
            note.show("Jump!", 800);
        }

        world.gravity = Vec2(0, nefu::fx::itofix(500));
        world.step(dt);

        // 粒子发射器跟随箱子
        emitter.position = ball->position;
        emitter.emit(psys.pool, GE_MAX_PARTICLES, dt, rng);
        psys.update(dt, 0, nefu::fx::itofix(100));
        note.update(dt);
    }

    void paint(Surface& s) {
        s.fill(0xFF101828);
        int W = s.width, H = s.height;

        // 瓦片风格背景：画几条横线表示地面
        for (int y = 60; y < H; y += 32)
            gfx::fillrect(s, 0, y, W, 1, 0xFF1E293B);

        // 画地板
        gfx::fillrect(s, fx::fixtoi(floor->position.x) - 400/2,
                         fx::fixtoi(floor->position.y) - 10,
                         400, 20, 0xFF475569);
        // 画平台
        gfx::fillrect(s, fx::fixtoi(plat->position.x) - 40, fx::fixtoi(plat->position.y) - 5, 80, 10, 0xFF64748B);
        // 画箱子
        int bx = fx::fixtoi(ball->position.x), by = fx::fixtoi(ball->position.y);
        gfx::fillrect(s, bx - 8, by - 8, 16, 16, 0xFF38BDF8);
        gfx::fillrect(s, fx::fixtoi(box2->position.x) - 6, fx::fixtoi(box2->position.y) - 6, 12, 12, 0xFFF59E0B);
        // 画掉落箱
        gfx::fillrect(s, fx::fixtoi(box2->position.x) - 6, fx::fixtoi(box2->position.y) - 6, 12, 12, 0xFFF59E0B);

        // 粒子
        draw_particles(s, psys);

        // UI：血条 + 按钮 + 菜单
        draw_healthbar(s, hp);
        draw_button(s, btn);
        // 菜单项高亮
        for (int i = 0; i < menu.count(); i++) {
            int c = (i == menu.selected) ? 0xFF334155 : 0xFF1E293B;
            gfx::fillrect(s, menu.x, menu.y + i * menu.item_h, 120, menu.item_h - 2, c);
        }

        char buf[128];
        ksprintf(buf, sizeof(buf), "FPS %d  bodies %d", fps, world.body_count());
        gfx::text(s, DEMO_W - 90, 10, buf, 0xFF94A3B8, 0xFF101828);
        ksprintf(buf, sizeof(buf), "GeDemo  Arrows move  Space jump  P burst  H hurt  R reset  Esc quit");
        gfx::text(s, 10, H - 16, buf, 0xFF94A3B8, 0xFF101828);
    }

    void key(const KeyEvent* e) {
        input.state.key_event(e->keycode, e->down);
        if (!e->down) return;
        if (e->ascii == 'h' || e->ascii == 'H') hp.set_value(hp.value - 15);
        if (e->ascii == 'p' || e->ascii == 'P') {
            // 一次性爆发粒子
            emitter.rate = fx::itofix(2000);
        }
        if (e->keycode == KEY_UP)   menu.move_up();
        if (e->keycode == KEY_DOWN) menu.move_down();
        if (e->ascii == 'r' || e->ascii == 'R') {
            ball->position = Vec2(fx::itofix(320), fx::itofix(100));
            ball->velocity = Vec2(0, 0);
        }
    }

};

} // namespace

static GeDemo* demo_of(Window* w) { return (GeDemo*)w->userdata; }

static void demo_paint(Window* w) { demo_of(w)->paint(w->back); }

static void demo_key(Window* w, const KeyEvent* e) {
    GeDemo* d = demo_of(w);
    d->key(e);
    if (e->keycode == KEY_ESC && e->down) g_wm->close_window(w);
}

static void demo_tick(Window* w) {
    GeDemo* d = demo_of(w);
    d->step(16);
}

static void demo_close(Window* w) {
    if (w->userdata) delete (GeDemo*)w->userdata;
    w->userdata = 0;
}

void gedemo_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("GameEngine Demo", x, y, DEMO_W, DEMO_H);
    if (!w) return;
    GeDemo* d = new GeDemo();
    w->userdata = d;
    w->on_paint = demo_paint;
    w->on_key = demo_key;
    w->on_tick = demo_tick;
    w->on_close = demo_close;
}

} // namespace nefu
