// nefuOS UI 组件库 —— 组件展示画廊
// 标签页切换(控件 / 表格 / 图表 / 对话框),展示 uiwidgets 全部组件交互。
// 所有组件渲染到一块 gfxlib 离屏 Buffer,再 blit 到窗口 Surface。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../gfxlib/gfxlib_all.h"
#include "../uiwidgets/uiwidgets_all.h"

namespace nefu {
namespace {

const int GW = 680, GH = 460;   // 离屏缓冲尺寸

// 画廊状态
struct Gallery {
    uint32_t* buf;
    int  tab;          // 0=控件 1=表格 2=图表 3=对话框
    ui::Widget* root;  // 当前标签页的控件树
    uint8_t prev_btn;

    void build_tab();  // 根据 tab 重建控件树
};

// ---- 各标签页构建 ----
static void build_controls_tab(ui::Widget* root) {
    root->set_bounds(0, 0, GW, GH);
    root->set_layout(ui::LayVBox, 6);
    root->set_margin(10);

    ui::Label* title = new ui::Label(root);
    title->set_text("Standard Controls");
    title->set_preferred(0, 18);

    ui::Button* btn = new ui::Button(root);
    btn->set_text("Click Me");
    btn->set_preferred(120, 28);

    ui::CheckBox* chk = new ui::CheckBox(root);
    chk->set_text("Enable feature");
    chk->set_preferred(160, 20);

    ui::RadioGroup* rg = new ui::RadioGroup();
    ui::RadioButton* r1 = new ui::RadioButton(root);
    r1->set_text("Option A"); r1->set_group(rg);
    ui::RadioButton* r2 = new ui::RadioButton(root);
    r2->set_text("Option B"); r2->set_group(rg);

    ui::Switch* sw = new ui::Switch(root);
    sw->set_preferred(60, 22);

    ui::Slider* sl = new ui::Slider(root);
    sl->set_range(0, 100); sl->set_value(40);
    sl->set_preferred(200, 20);

    ui::ProgressBar* pb = new ui::ProgressBar(root);
    pb->set_range(0, 100); pb->set_value(65);
    pb->set_preferred(200, 14);

    ui::Spinner* sp = new ui::Spinner(root);
    sp->set_range(0, 50); sp->set_value(10);

    ui::TextField* tf = new ui::TextField(root);
    tf->set_placeholder("type here...");
    tf->set_preferred(200, 24);

    ui::ComboBox* cb = new ui::ComboBox(root);
    cb->add_item("Red"); cb->add_item("Green"); cb->add_item("Blue");
    cb->set_preferred(160, 24);
}

static void build_table_tab(ui::Widget* root) {
    root->set_bounds(0, 0, GW, GH);
    root->set_layout(ui::LayVBox, 6);
    root->set_margin(10);

    ui::Label* title = new ui::Label(root);
    title->set_text("TableView");
    title->set_preferred(0, 18);

    ui::TableView* tv = new ui::TableView(root);
    tv->set_bounds(0, 0, GW - 20, GH - 60);
    const char* headers[4] = { "Name", "Dept", "Age", "City" };
    tv->set_columns(headers, 4);
    const char* names[6] = { "Alice", "Bob", "Carol", "Dave", "Eve", "Frank" };
    const char* depts[6] = { "Eng", "Art", "Eng", "QA", "Art", "QA" };
    for (int i = 0; i < 6; i++) {
        char age[8]; ksprintf(age, sizeof(age), "%d", 25 + i);
        const char* row[4] = { names[i], depts[i], age, "Qingdao" };
        tv->add_row(row, 4);
    }
}

static void build_chart_tab(ui::Widget* root) {
    root->set_bounds(0, 0, GW, GH);
    root->set_layout(ui::LayVBox, 6);
    root->set_margin(10);

    ui::Label* title = new ui::Label(root);
    title->set_text("Charts");
    title->set_preferred(0, 18);

    ui::Chart* line = new ui::Chart(root);
    line->set_type(ui::ChartLine);
    line->set_bounds(0, 0, GW - 20, 180);
    int s1 = line->add_series("2024", 0xFF3498DBu);
    int s2 = line->add_series("2025", 0xFFE74C3Cu);
    for (int i = 0; i < 12; i++) {
        line->add_value(s1, 30 + (i * 7) % 60);
        line->add_value(s2, 40 + (i * 11) % 50);
    }

    ui::Chart* pie = new ui::Chart(root);
    pie->set_type(ui::ChartPie);
    pie->set_bounds(0, 0, GW - 20, 160);
    int ps = pie->add_series("share", 0);
    pie->add_value(ps, 35); pie->add_value(ps, 25); pie->add_value(ps, 40);
}

static void build_dialog_tab(ui::Widget* root) {
    root->set_bounds(0, 0, GW, GH);
    root->set_layout(ui::LayVBox, 6);
    root->set_margin(10);

    ui::Label* title = new ui::Label(root);
    title->set_text("Dialogs");
    title->set_preferred(0, 18);

    ui::MessageDialog* md = new ui::MessageDialog(root);
    md->set_message(ui::MsgInfo, "Welcome to the UI widget gallery!");
    md->set_bounds(10, 40, 320, 120);

    ui::ProgressDialog* pd = new ui::ProgressDialog(root);
    pd->set_title("Installing");
    pd->set_range(0, 100); pd->set_value(45);
    pd->set_bounds(10, 180, 320, 90);

    ui::ColorDialog* cd = new ui::ColorDialog(root);
    cd->set_bounds(360, 40, 220, 200);
}

void Gallery::build_tab() {
    if (root) { delete root; root = 0; }
    root = new ui::Widget();
    switch (tab) {
        case 0: build_controls_tab(root); break;
        case 1: build_table_tab(root); break;
        case 2: build_chart_tab(root); break;
        default: build_dialog_tab(root); break;
    }
    root->layout();
}

// ---- 渲染到离屏 Buffer ----
void render_buf(Gallery* g) {
    gfxlib::Buffer b = { g->buf, GW, GH };
    gfxlib::draw_rect_fill(b, 0, 0, GW, GH, ui::theme().bg);
    // 顶部标签栏
    gfxlib::draw_rect_fill(b, 0, 0, GW, 26, 0xFF2C3E50u);
    const char* tabs[4] = { "1.Controls", "2.Table", "3.Chart", "4.Dialog" };
    int tx = 10;
    for (int i = 0; i < 4; i++) {
        if (i == g->tab) gfxlib::draw_rect_fill(b, tx - 4, 3, g->root->text_width(tabs[i]) + 12, 20, 0xFF3498DBu);
        g->root->draw_text(b, tx, 10, tabs[i], i == g->tab ? 0xFFFFFFFFu : 0xFFBDC3C7u);
        tx += g->root->text_width(tabs[i]) + 24;
    }
    // 控件树(从 y=26 开始)
    gfxlib::draw_rect_fill(b, 0, 26, GW, GH - 26, 0xFFECF0F1u);
    g->root->paint_tree(b, 0, 26);
}

} // namespace

// ---- 窗口胶水 ----
static Gallery* gal_of(Window* w) { return (Gallery*)w->userdata; }

static void gal_paint(Window* w) {
    Gallery* g = gal_of(w);
    render_buf(g);
    // blit 到窗口 Surface
    Surface& s = w->back;
    for (int y = 0; y < GH && y < s.height; y++) {
        const uint32_t* row = g->buf + (size_t)y * GW;
        for (int x = 0; x < GW && x < s.width; x++)
            s.setpx(x, y, row[x]);
    }
    char cap[96];
    ksprintf(cap, sizeof(cap), "Widget Gallery  [1-4] tab  [ESC] close");
    gfx::text(s, 8, GH - 16, cap, 0xFF7F8C8D, 0xFFECF0F1);
}

static void gal_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    Gallery* g = gal_of(w);
    if (e->ascii >= '1' && e->ascii <= '4') {
        g->tab = e->ascii - '1';
        g->build_tab();
    } else if (e->keycode == UX_KEY_ESC || e->ascii == 27) {
        g_wm->close_window(w);
        return;
    }
    // 其余键转发给控件树
    if (g->root) g->root->dispatch_key(e->keycode, e->ascii, true);
    w->on_paint(w);
}

static void gal_mouse(Window* w, int mx, int my, uint8_t buttons) {
    Gallery* g = gal_of(w);
    if (!g->root) return;
    bool down = (buttons & 1) != 0;
    // 仅在按钮状态变化时分发(按下/释放)
    if (down != ((g->prev_btn & 1) != 0)) {
        g->root->dispatch_mouse(mx, my, 0, down);
        w->on_paint(w);
    }
    g->prev_btn = buttons;
}

static void gal_close(Window* w) {
    Gallery* g = (Gallery*)w->userdata;
    if (g) {
        if (g->root) delete g->root;
        delete[] g->buf;
        delete g;
    }
    w->userdata = 0;
}

void widgetgallery_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Widget Gallery", x, y, GW, GH);
    if (!w) return;
    Gallery* g = new Gallery();
    g->buf = new uint32_t[(size_t)GW * GH];
    g->tab = 0;
    g->root = 0;
    g->prev_btn = 0;
    g->build_tab();
    w->userdata = g;
    w->on_paint = gal_paint;
    w->on_key = gal_key;
    w->on_mouse = gal_mouse;
    w->on_close = gal_close;
    g_wm->raise(w);
}

} // namespace nefu
