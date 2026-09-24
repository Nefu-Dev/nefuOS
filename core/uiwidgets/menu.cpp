// nefuOS UI 组件库 —— 菜单实现
#include "menu.h"
#include <string.h>

namespace nefu {
namespace ui {

// ===================== MenuItem =====================
MenuItem::MenuItem() : shortcut(0), separator(false), enabled(true), on_click(0), user(0), parent(0) {}
MenuItem::MenuItem(const char* text_) : shortcut(0), separator(false), enabled(true), on_click(0), user(0), parent(0) {
    text = text_;
}
MenuItem::~MenuItem() {
    for (int i = 0; i < children.size(); i++) delete children[i];
    children.clear();
}
void MenuItem::add(MenuItem* item) {
    if (!item) return;
    item->parent = this;
    children.push(item);
}
MenuItem* MenuItem::add_item(const char* text_, int shortcut_) {
    MenuItem* m = new MenuItem(text_);
    m->shortcut = shortcut_;
    add(m);
    return m;
}
void MenuItem::add_separator() {
    MenuItem* m = new MenuItem();
    m->separator = true;
    add(m);
}
int MenuItem::depth() const {
    int d = 0;
    for (const MenuItem* p = parent; p; p = p->parent) d++;
    return d;
}

// ===================== MenuBar =====================
MenuBar::MenuBar(Widget* parent) : Widget(parent), open_index_(-1) {
    pref_w_ = 400; pref_h_ = 22;
}
MenuItem* MenuBar::add_menu(const char* text_) {
    MenuItem* m = new MenuItem(text_);
    items_.push(m);
    return m;
}
int MenuBar::find_shortcut(int key) const {
    if (key == 0) return -1;
    for (int i = 0; i < items_.size(); i++) {
        // 顶层及其子菜单递归查找
        List<const MenuItem*> stack;
        stack.push(items_[i]);
        while (stack.size() > 0) {
            const MenuItem* cur = stack[stack.size() - 1];
            stack.remove(stack.size() - 1);
            if (cur->shortcut == key && !cur->separator) return i;
            for (int c = 0; c < cur->children.size(); c++) stack.push(cur->children[c]);
        }
    }
    return -1;
}
bool MenuBar::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (!e.down) return true;
    // 按 x 命中哪个顶层菜单
    int xacc = 8;
    for (int i = 0; i < items_.size(); i++) {
        int w_ = text_width(items_[i]->text.c_str()) + 20;
        if (e.x >= xacc && e.x < xacc + w_) { open_index_ = (open_index_ == i) ? -1 : i; invalidate(); return true; }
        xacc += w_;
    }
    open_index_ = -1;
    invalidate();
    return true;
}
void MenuBar::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.panel);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    int xacc = ox + 8;
    for (int i = 0; i < items_.size(); i++) {
        const char* s = items_[i]->text.c_str();
        int w_ = text_width(s) + 20;
        if (i == open_index_) gfxlib::draw_rect_fill(b, xacc - 4, oy + 2, w_, h - 4, t.selection);
        draw_text(b, xacc, oy + (h - 7) / 2, s, t.text);
        xacc += w_;
    }
}

// ===================== PopupMenu =====================
PopupMenu::PopupMenu(Widget* parent) : Widget(parent) {
    root = new MenuItem();
    pref_w_ = 160; pref_h_ = 100;
}
PopupMenu::~PopupMenu() { delete root; }
void PopupMenu::add_item(const char* text_, int shortcut_) {
    root->add_item(text_, shortcut_);
}
void PopupMenu::add_separator() {
    root->add_separator();
}
int PopupMenu::visible_count() const {
    return root->children.size();
}
void PopupMenu::open_at(int x_, int y_) {
    move_to(x_, y_);
    int rh = row_h();
    h = root->children.size() * rh;
    if (h < rh) h = rh;
    invalidate();
}
bool PopupMenu::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (!e.down) return true;
    int row = e.y / row_h();
    if (row >= 0 && row < root->children.size()) {
        MenuItem* m = root->children[row];
        if (!m->separator && m->on_click) m->on_click(m, m->user);
    }
    return true;
}
void PopupMenu::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.panel);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    int rh = row_h();
    for (int i = 0; i < root->children.size(); i++) {
        MenuItem* m = root->children[i];
        int ry = oy + i * rh;
        if (m->separator) {
            gfxlib::draw_line(b, ox + 4, ry + rh / 2, ox + w - 4, ry + rh / 2, t.border);
            continue;
        }
        draw_text(b, ox + 8, ry + (rh - 7) / 2, m->text.c_str(), m->enabled ? t.text : t.text_dim);
        if (m->shortcut) {
            char sc[8];
            ksprintf(sc, sizeof(sc), "%c", (char)m->shortcut);
            draw_text(b, ox + w - 20, ry + (rh - 7) / 2, sc, t.text_dim);
        }
    }
}

// ===================== 模块自检 =====================
int menu_self_test() {
    int fail = 0;

    // --- MenuBar:添加顶层菜单 ---
    {
        MenuBar mb;
        mb.set_bounds(0, 0, 400, 22);
        MenuItem* file = mb.add_menu("File");
        mb.add_menu("Edit");
        mb.add_menu("Help");
        if (mb.item_count() != 3) fail++;
        // 给 File 加子项
        file->add_item("New", 'n');
        file->add_item("Open", 'o');
        file->add_separator();
        file->add_item("Exit", 'x');
        if (file->children.size() != 4) fail++;
        if (!file->children[2]->separator) fail++;
        // 快捷键查找
        if (mb.find_shortcut('o') != 0) fail++;   // Open 在 File(索引0)
        if (mb.find_shortcut('x') != 0) fail++;
        if (mb.find_shortcut('z') != -1) fail++;
    }

    // --- 子菜单嵌套深度 ---
    {
        MenuItem root;
        MenuItem* a = root.add_item("A");
        MenuItem* b = a->add_item("B");
        MenuItem* c = b->add_item("C");
        if (c->depth() != 3) fail++;   // c->b->a->root = 3 层
    }

    // --- PopupMenu:条目与分隔线计数 ---
    {
        PopupMenu pm;
        pm.add_item("Copy", 'c');
        pm.add_item("Paste", 'v');
        pm.add_separator();
        pm.add_item("Delete", 'd');
        if (pm.visible_count() != 4) fail++;
        pm.open_at(10, 10);
        if (pm.h != 4 * 16) fail++;
    }

    // --- MenuItem 回调触发 ---
    {
        int fired = 0;
        PopupMenu pm;
        pm.add_item("Do");
        pm.root->children[0]->on_click = [](void* w, void* u) { (void)w; (*(int*)u)++; };
        pm.root->children[0]->user = &fired;
        // 模拟点第 0 行
        pm.on_mouse(UxMouseEvent{ 5, 5, 0, true });
        if (fired != 1) fail++;
    }

    return fail;
}

} // namespace ui
} // namespace nefu
