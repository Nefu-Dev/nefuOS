// nefuOS deepviz —— 训练一个小型 MLP 求解 XOR，实时绘制损失曲线与网络结构。
// 控制：
//   Space/Enter : 开始/暂停训练
//   R           : 重新初始化网络
//   Esc         : 关闭窗口
// 每帧执行若干训练步（无异常、无线程），损失历史画成折线。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../deeplearn/deeplearn_all.h"

namespace nefu {
namespace deeplearn {
// 前向声明，避免头文件循环
}
}

namespace nefu {

namespace {

const int VIZ_W = 640, VIZ_H = 460;
const int HIST_N = 120;   // 损失曲线保留点数

struct DeepViz {
    // XOR 数据集
    fix xor_x[4][2];
    int xor_y[4];
    // 网络：2 -> 3 -> 1（手工两层 Dense + tanh + 线性输出）
    deeplearn::Dense l1;
    deeplearn::Dense l2;
    deeplearn::Adam opt;
    bool running;
    bool done;
    int  step;
    fix  history[HIST_N];
    int  hist_n;
    fix  cur_loss;

    DeepViz()
        : l1(2, 3, 20240601u), l2(3, 1, 20240602u),
          opt(fx::fxf(3,100)) {
        xor_x[0][0]=fx::itofix(0); xor_x[0][1]=fx::itofix(0); xor_y[0]=0;
        xor_x[1][0]=fx::itofix(0); xor_x[1][1]=fx::itofix(1); xor_y[1]=1;
        xor_x[2][0]=fx::itofix(1); xor_x[2][1]=fx::itofix(0); xor_y[2]=1;
        xor_x[3][0]=fx::itofix(1); xor_x[3][1]=fx::itofix(1); xor_y[3]=0;
        running=false; done=false; step=0; hist_n=0; cur_loss=0;
        for (int i=0;i<HIST_N;i++) history[i]=0;
        List<deeplearn::Tensor*> ps;
        l1.params(ps); l2.params(ps);
        opt.add_all(ps);
    }

    void reset() {
        running=false; done=false; step=0; hist_n=0;
        for (int i=0;i<HIST_N;i++) history[i]=0;
    }

    // 执行一个训练步，返回损失
    fix train_step() {
        deeplearn::tape_reset();
        int sx[2]={4,2};
        deeplearn::Tensor X = deeplearn::t_from_flat(2,sx,(fix*)xor_x[0]);
        deeplearn::Tensor h = l1.forward(X);
        deeplearn::Tensor ht = deeplearn::act_tanh(h);
        deeplearn::Tensor y = l2.forward(ht);   // [4,1]
        // 目标：[0,1,1,0]
        fix tv[4] = {0, fx::FX_ONE, fx::FX_ONE, 0};
        int sy[2]={4,1};
        deeplearn::Tensor T = deeplearn::t_from_flat(2,sy,tv);
        deeplearn::Tensor loss = deeplearn::loss_mse(y, T);
        deeplearn::backward(loss);
        opt.step();
        opt.zero_grad();
        deeplearn::tape_reset();
        return loss.data[0];
    }

    // 每帧跑若干步
    void tick() {
        if (!running || done) return;
        for (int t=0;t<3;t++) {
            fix l = train_step();
            cur_loss = l;
            history[hist_n % HIST_N] = l;
            hist_n++;
            step++;
            if (step > 400) { done = true; running = false; }
        }
    }

    void paint(Surface& s) {
        gfx::fillrect(s, 0, 0, VIZ_W, VIZ_H, color::CREAM);
        gfx::text(s, 10, 8, "DeepViz: XOR with 2-3-1 MLP", color::TEXT, color::CREAM);
        char buf[80];
        nefu::ksprintf(buf, sizeof(buf), "step=%d  loss=%.3f", step, cur_loss/65536.0);
        gfx::text(s, 10, 26, buf, color::TEXT2, color::CREAM);

        // ---- 损失曲线 ----
        int px0=20, py0=60, pw=380, ph=160;
        gfx::rect(s, px0, py0, pw, ph, color::BORDER);
        gfx::text(s, px0, py0-14, "loss", color::TEXT2, color::CREAM);
        // 找最大值归一
        fix mx = 1;
        for (int i=0;i<hist_n && i<HIST_N;i++) if (history[i]>mx) mx=history[i];
        int prev_x=0, prev_y=0;
        for (int i=0;i<hist_n && i<HIST_N;i++) {
            int x = px0 + (i*pw)/HIST_N;
            int y = py0 + ph - (int)((long)history[i]*ph/(mx>0?mx:1));
            if (y > py0+ph) y = py0+ph;
            if (i>0) gfx::line(s, prev_x, prev_y, x, y, color::RED);
            prev_x=x; prev_y=y;
        }

        // ---- 网络结构示意 ----
        int nx=440, ny=70;
        // 输入层 2 节点，隐层 3，输出 1
        gfx::text(s, nx-10, ny-20, "network", color::TEXT2, color::CREAM);
        int in[2][2] = {{nx, ny+20}, {nx, ny+80}};
        int hd[3][2] = {{nx+80, ny+10}, {nx+80, ny+50}, {nx+80, ny+90}};
        int out[1][2] = {{nx+160, ny+50}};
        // 连线
        for (int i=0;i<2;i++) for (int j=0;j<3;j++)
            gfx::line(s, in[i][0],in[i][1], hd[j][0],hd[j][1], color::LIGHT);
        for (int j=0;j<3;j++)
            gfx::line(s, hd[j][0],hd[j][1], out[0][0],out[0][1], color::LIGHT);
        // 节点
        for (int i=0;i<2;i++) gfx::fillcircle(s, in[i][0],in[i][1], 6, color::BLUE);
        for (int j=0;j<3;j++) gfx::fillcircle(s, hd[j][0],hd[j][1], 6, color::GREEN);
        gfx::fillcircle(s, out[0][0],out[0][1], 6, color::ORANGE);

        // ---- 决策区域（网格） ----
        int gx0=20, gy0=260, gw=20, gh=20;
        gfx::text(s, gx0, gy0-16, "decision map", color::TEXT2, color::CREAM);
        // 用当前前向逐格推理
        for (int r=0;r<8;r++) for (int c=0;c<8;c++) {
            fix xv = fx::fx_div(fx::itofix(c), fx::itofix(7));
            fix yv = fx::fx_div(fx::itofix(r), fx::itofix(7));
            deeplearn::Tensor X = deeplearn::t_zeros(2,(int[2]){1,2});
            X.data[0]=xv; X.data[1]=yv;
            deeplearn::Tensor h = l1.forward(X);
            deeplearn::Tensor ht = deeplearn::act_tanh(h);
            deeplearn::Tensor y = l2.forward(ht);
            fix v = y.data[0];
            uint32_t col = (v > 0) ? color::GREEN : color::LIGHT;
            gfx::fillrect(s, gx0+c*gw, gy0+r*gh, gw-1, gh-1, col);
        }


        // ---- 右侧统计面板 ----
        int sx=440, sy=260;
        gfx::text(s, sx, sy, "stats", color::TEXT2, color::CREAM);
        // 跑一遍前向统计训练集准确率
        int correct=0;
        for (int i=0;i<4;i++) {
            deeplearn::Tensor X = deeplearn::t_zeros(2,(int[2]){1,2});
            X.data[0]=fx::itofix(xor_x[i][0]!=0?1:0);
            X.data[1]=fx::itofix(xor_x[i][1]!=0?1:0);
            deeplearn::Tensor h = l1.forward(X);
            deeplearn::Tensor ht = deeplearn::act_tanh(h);
            deeplearn::Tensor y = l2.forward(ht);
            bool pos = y.data[0] > 0;
            if ((pos && xor_y[i]) || (!pos && !xor_y[i])) correct++;
        }
        char sb[80];
        nefu::ksprintf(sb, sizeof(sb), "train acc: %d/4", correct);
        gfx::text(s, sx, sy+18, sb, color::TEXT, color::CREAM);
        nefu::ksprintf(sb, sizeof(sb), "lr: 0.03  Adam");
        gfx::text(s, sx, sy+36, sb, color::TEXT2, color::CREAM);
        nefu::ksprintf(sb, sizeof(sb), "net: 2-3-1 tanh");
        gfx::text(s, sx, sy+54, sb, color::TEXT2, color::CREAM);
        // 进度条
        int bw=160, bx=sx, by=sy+80;
        gfx::rect(s, bx, by, bw, 8, color::BORDER);
        int fill = (step*bw)/400; if(fill>bw) fill=bw;
        gfx::fillrect(s, bx, by, fill, 8, color::GREEN);
        nefu::ksprintf(sb, sizeof(sb), "progress %d%%", (step*100)/400);
        gfx::text(s, bx, by+12, sb, color::TEXT2, color::CREAM);
        gfx::text(s, 10, VIZ_H-18, "Space: run/pause   R: reset   Esc: close",
                  color::TEXT2, color::CREAM);
    }
};

} // namespace

static DeepViz* viz_of(Window* w) { return (DeepViz*)w->userdata; }

static void viz_paint(Window* w) { viz_of(w)->paint(w->back); }

static void viz_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    DeepViz* v = viz_of(w);
    if (e->ascii=='r'||e->ascii=='R') { v->reset(); return; }
    if (e->keycode==KEY_SPACE||e->keycode==KEY_ENTER) { v->running=!v->running; return; }
    if (e->keycode==KEY_ESC) g_wm->close_window(w);
}

static void viz_tick(Window* w) { viz_of(w)->tick(); }

static void viz_close(Window* w) {
    if (w->userdata) delete (DeepViz*)w->userdata;
    w->userdata = 0;
}

void deepviz_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("DeepViz", x, y, VIZ_W, VIZ_H);
    if (!w) return;
    DeepViz* v = new DeepViz();
    w->userdata = v;
    w->on_paint = viz_paint;
    w->on_key = viz_key;
    w->on_tick = viz_tick;
    w->on_close = viz_close;
    g_wm->raise(w);
}

} // namespace nefu
