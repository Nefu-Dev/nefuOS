// nefuOS simulation laboratory — 仿真引擎可视化实验台
// Controls:
//   1..5  : 选择仿真 (Life / 粒子 / 单摆 / L-系统 / Boids)
//   Space : 开始/暂停      R : 重置      ESC : 关闭
//   鼠标左键点击: 在画布上交互 (Life 翻转细胞 / 粒子发射点 / Boids 添加)
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../gfxlib/gfxlib_all.h"
#include "../simulate/simulate_all.h"
#include <cmath>

namespace nefu {

namespace {

using namespace simulate;
const int LAB_W = 640, LAB_H = 440;

const char* SIM_NAMES[5] = {
    "Conway Life", "Particle System", "Pendulum",
    "L-System Tree", "Boids Flock"
};

struct SimLab {
    uint32_t* buf;
    int sim;                 // 0..4
    uint32_t tick;
    bool running;

    // 各仿真的状态 (用库对象)
    simulate::GameOfLife life;
    simulate::ParticleSystem psys;
    simulate::Pendulum pend;
    simulate::LSystem ls;
    simulate::TurtleInterpreter turtle;
    simulate::Flocking flock;
    double pend_t;

    void reset(int s);
    void step_one();
    void render();
    void paint(Surface& s);
    void on_click(int mx, int my);
};

void SimLab::reset(int s) {
    sim = s;
    running = true;
    tick = 0;
    // Life: 60x40 环形边界，撒一个滑翔机
    if (sim == 0) {
        life.init(60, 40, simulate::Boundary::Torus);
        life.set(20, 20, true); life.set(21, 20, true); life.set(22, 20, true);
        life.set(22, 19, true); life.set(21, 18, true);
    } else if (sim == 1) {
        psys.init(2048, 4, 4, 2, (double)LAB_W, (double)LAB_H);
        simulate::Emitter e;
        e.type = simulate::EmitterType::Point;
        e.origin = simulate::Vec2(LAB_W / 2.0, 40);
        e.rate = 300; e.speed_min = 30; e.speed_max = 90;
        e.life_min = 1.2; e.life_max = 2.4;
        psys.add_emitter(e);
        simulate::ForceField g; g.type = simulate::ForceType::Gravity; g.vector = simulate::Vec2(0, 120);
        psys.add_field(g);
        simulate::PlaneCollider pl; pl.axis = simulate::PlaneCollider::Y; pl.position = LAB_H - 20; pl.restitution = 0.6;
        psys.add_plane(pl);
    } else if (sim == 2) {
        pend.init(1.0, 1.2);
        pend_t = 0;
    } else if (sim == 3) {
        ls.init();
        ls.preset_tree();
        ls.iterate(3);
        turtle.on_line = 0; turtle.user = 0;
    } else if (sim == 4) {
        flock.init(128, 8, LAB_W, LAB_H);
        for (int i = 0; i < 40; i++) {
            simulate::Boid b;
            b.pos = simulate::Vec2(100 + (i * 37) % (LAB_W - 200), 100 + (i * 53) % (LAB_H - 200));
            b.vel = simulate::Vec2((i % 2 ? 1 : -1) * 40, (i % 3 ? 1 : -1) * 30);
            flock.add_boid(b);
        }
    }
}

void SimLab::step_one() {
    if (sim == 0) life.step();
    else if (sim == 1) psys.update(0.016);
    else if (sim == 2) { pend.step(0.016); pend_t += 0.016; }
    else if (sim == 4) flock.step(0.016);
}

void SimLab::render() {
    gfxlib::Buffer b = { buf, LAB_W, LAB_H };
    gfxlib::draw_rect_fill(b, 0, 0, LAB_W, LAB_H, 0xFF101820);

    if (sim == 0) {
        int cw = LAB_W / life.w, ch = LAB_H / life.h;
        for (int y = 0; y < life.h; y++)
            for (int x = 0; x < life.w; x++)
                if (life.get(x, y))
                    gfxlib::draw_rect_fill(b, x * cw, y * ch, cw, ch, 0xFF2ECC71);
    } else if (sim == 1) {
        for (int i = 0; i < psys.count; i++) {
            const simulate::Particle& p = psys.particles[i];
            if (!p.active) continue;
            int px = (int)p.pos.x, py = (int)p.pos.y;
            gfxlib::draw_circle_fill(b, px, py, (int)p.size, 0xFF3498DB);
        }
    } else if (sim == 2) {
        int ox = LAB_W / 2, oy = 80;
        double scale = 130.0;
        int tx = ox + (int)(pend.tip_x() * scale), ty = oy + (int)(pend.tip_y() * scale);
        gfxlib::draw_line(b, ox, oy, tx, ty, 0xFFE74C3C);
        gfxlib::draw_circle_fill(b, tx, ty, 8, 0xFFF1C40F);
        gfxlib::draw_circle_fill(b, ox, oy, 4, 0xFFFFFFFF);
    } else if (sim == 3) {
        // 海龟解释器：用回调收集线段
        // 简化：直接在缓冲上画（复用库的解释器，但我们内联）
        double x = LAB_W / 2.0, y = (double)LAB_H - 20;
        double ang = -1.5707963;
        double step = 2.0;
        double stk_x[64], stk_y[64], stk_a[64];
        int sp = 0;
        for (int i = 0; i < ls.out_len; i++) {
            char c = ls.output[i];
            if (c == 'F' || c == 'A' || c == 'B') {
                double nx = x + step * cos(ang);
                double ny = y + step * sin(ang);
                gfxlib::draw_line(b, (int)x, (int)y, (int)nx, (int)ny, 0xFF27AE60);
                x = nx; y = ny;
            } else if (c == '+') ang += ls.angle;
            else if (c == '-') ang -= ls.angle;
            else if (c == '[') { if (sp < 64) { stk_x[sp]=x; stk_y[sp]=y; stk_a[sp]=ang; sp++; } }
            else if (c == ']') { if (sp > 0) { sp--; x=stk_x[sp]; y=stk_y[sp]; ang=stk_a[sp]; } }
        }
    } else if (sim == 4) {
        for (int i = 0; i < flock.count; i++) {
            const simulate::Boid& bd = flock.boids[i];
            int px = (int)bd.pos.x, py = (int)bd.pos.y;
            uint32_t col = bd.is_predator ? 0xFFE74C3C : 0xFF3498DB;
            gfxlib::draw_circle_fill(b, px, py, 3, col);
            gfxlib::draw_line(b, px, py, px + (int)bd.vel.x, py + (int)bd.vel.y, col);
        }
    }
}

void SimLab::paint(Surface& s) {
    for (int y = 0; y < LAB_H && y < s.height; y++) {
        const uint32_t* row = buf + (size_t)y * LAB_W;
        for (int x = 0; x < LAB_W && x < s.width; x++)
            s.setpx(x, y, row[x]);
    }
    char cap[160];
    ksprintf(cap, sizeof(cap), "SimLab: %s  [1-5] switch  [Space] %s  [R] reset  [click] interact",
             SIM_NAMES[sim], running ? "pause" : "run");
    gfx::text(s, 8, 4, cap, 0x00DDDDDD, 0x00101820);
}

void SimLab::on_click(int mx, int my) {
    if (sim == 0) {
        int cw = LAB_W / life.w, ch = LAB_H / life.h;
        int gx = mx / cw, gy = my / ch;
        life.set(gx, gy, !life.get(gx, gy));
    } else if (sim == 1) {
        simulate::Particle p;
        p.pos = simulate::Vec2((double)mx, (double)my);
        p.vel = simulate::Vec2(0, 0);
        p.life = 2; p.max_life = 2; p.active = true; p.size = 3;
        psys.spawn(p);
    } else if (sim == 4) {
        simulate::Boid bd;
        bd.pos = simulate::Vec2((double)mx, (double)my);
        bd.vel = simulate::Vec2(40, 0);
        flock.add_boid(bd);
    }
}

} // namespace

static SimLab* lab_of(Window* w) { return (SimLab*)w->userdata; }

static void sim_paint(Window* w) {
    SimLab* l = lab_of(w);
    l->render();
    l->paint(w->back);
}
static void sim_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    SimLab* l = lab_of(w);
    if (e->ascii == 'r' || e->ascii == 'R') { l->reset(l->sim); return; }
    if (e->keycode == KEY_SPACE) { l->running = !l->running; return; }
    if (e->ascii >= '1' && e->ascii <= '5') { l->reset(e->ascii - '1'); return; }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}
static void sim_tick(Window* w) {
    SimLab* l = lab_of(w);
    if (l && l->running) { l->tick++; l->step_one(); }
}
static void sim_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (buttons & 1) lab_of(w)->on_click(mx, my);
}
static void sim_close(Window* w) {
    SimLab* l = (SimLab*)w->userdata;
    if (l) {
        if (l->sim == 1) l->psys.shutdown();
        if (l->sim == 3) l->ls.shutdown();
        if (l->sim == 4) l->flock.shutdown();
        delete[] l->buf;
        delete l;
    }
    w->userdata = 0;
}

void simlab_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Simulation Lab", x, y, LAB_W, LAB_H);
    if (!w) return;
    SimLab* l = new SimLab();
    l->buf = new uint32_t[(size_t)LAB_W * LAB_H];
    l->reset(0);
    w->userdata = l;
    w->on_paint = sim_paint;
    w->on_key = sim_key;
    w->on_tick = sim_tick;
    w->on_mouse = sim_mouse;
    w->on_close = sim_close;
    g_wm->raise(w);
}

} // namespace nefu
