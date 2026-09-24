// nefuOS 数据库管理器 —— 窗口化 SQL 控制台
//
// 界面:
//   顶部:标题栏
//   中部:SQL 输入框(可键入,Enter 执行,退格删除)
//   下部:结果表格(列名 + 行数据),支持 CREATE / INSERT / SELECT / DELETE
//
// 内置一个示例员工表,启动即可 SELECT。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../database/sqlexec.h"

namespace nefu {

namespace {

const int DBG_W = 640, DBG_H = 440;   // 窗口尺寸
const int INPUT_MAX = 256;             // SQL 输入缓冲

// 窗口状态:持有一个数据库实例 + 输入缓冲 + 最近结果
struct DbMgr {
    database::Database* db;
    char     input[INPUT_MAX];
    int      input_len;
    bool     cursor_on;                  // 文本光标闪烁
    // 最近一次 SELECT 的结果(用于重绘)
    List<String> result_cols;
    List<database::SqlRow> result_rows;
    String   status;                     // 底部状态行(成功/失败信息)
    int      scroll;                     // 结果区滚动行数

    DbMgr();
    ~DbMgr();
    void exec_input();
    void seed_demo();
    void paint(Surface& s);
};

DbMgr::DbMgr() : db(0), input_len(0), cursor_on(true), scroll(0) {
    input[0] = 0;
    db = new database::Database();
    seed_demo();
}

DbMgr::~DbMgr() { delete db; db = 0; }

// 建一个示例员工表,方便打开就能 SELECT
void DbMgr::seed_demo() {
    db->exec("CREATE TABLE emp (id, name, dept, salary)");
    db->exec("INSERT INTO emp VALUES (1, 'Alice', 'Eng', 9000)");
    db->exec("INSERT INTO emp VALUES (2, 'Bob',  'Sales', 5000)");
    db->exec("INSERT INTO emp VALUES (3, 'Carol','Eng', 12000)");
    db->exec("INSERT INTO emp VALUES (4, 'Dan',  'Eng', 8000)");
    db->exec("INSERT INTO emp VALUES (5, 'Eve',  'HR', 6000)");
    status = String("ready. try: SELECT * FROM emp WHERE dept='Eng' ORDER BY salary");
}

// 执行输入框里的 SQL,把结果填到 result_cols/result_rows
void DbMgr::exec_input() {
    String sql(input, input_len);
    if (sql.len() == 0) return;
    database::SqlResult r = db->exec(sql.c_str());
    result_cols = r.col_names;
    result_rows = r.rows;
    if (!r.ok) {
        status = String("ERR: ") + r.error;
    } else {
        status = String("OK: ") + r.message;
    }
    // 清空输入框
    input[0] = 0;
    input_len = 0;
    scroll = 0;
}

void DbMgr::paint(Surface& s) {
    s.fill(0x00FAF8EF);
    int W = s.width, H = s.height;
    gfx::text_scale(s, 10, 8, "nefuDB Manager", 0x0037474F, 0x00FAF8EF, 2);

    // ---- 输入框 ----
    int in_x = 10, in_y = 40, in_w = W - 20, in_h = 26;
    gfx::fillrect(s, in_x, in_y, in_w, in_h, 0x00FFFFFF);
    gfx::rect(s, in_x, in_y, in_w, in_h, 0x0080CBC4);
    gfx::text(s, in_x + 6, in_y + 8, input, 0x00202020, 0x00FFFFFF);
    // 文本光标
    if (cursor_on) {
        int cx = in_x + 6 + input_len * 7;
        gfx::fillrect(s, cx, in_y + 5, 1, in_h - 10, 0x00202020);
    }
    gfx::text(s, in_x, in_y + in_h + 4,
              "Enter=execute  Backspace=del  Esc=close",
              0x00909090, 0x00FAF8EF);

    // ---- 结果区:列名 + 行 ----
    int res_y = in_y + in_h + 22;
    int row_h = 16;
    int col_w = (W - 30) / (result_cols.size() > 0 ? result_cols.size() : 1);
    if (col_w < 60) col_w = 60;

    // 列头
    gfx::fillrect(s, 10, res_y, W - 20, row_h, 0x0080CBC4);
    for (int c = 0; c < result_cols.size(); c++) {
        gfx::text(s, 14 + c * col_w, res_y + 4,
                  result_cols[c].c_str(), 0x00FFFFFF, 0x0080CBC4);
    }

    // 行(按 scroll 截断,避免超出窗口)
    int max_rows = (H - res_y - 40) / row_h;
    for (int r = 0; r < result_rows.size() && r < max_rows; r++) {
        int y = res_y + row_h + r * row_h;
        if (r % 2 == 0) gfx::fillrect(s, 10, y, W - 20, row_h, 0x00F0EFE8);
        for (int c = 0; c < result_rows[r].cells.size(); c++) {
            gfx::text(s, 14 + c * col_w, y + 4,
                      result_rows[r].cells[c].c_str(), 0x00303030,
                      (r % 2 == 0) ? 0x00F0EFE8 : 0x00FAF8EF);
        }
    }

    // ---- 底部状态行 ----
    gfx::fillrect(s, 0, H - 18, W, 18, 0x0037474F);
    uint32_t sc = 0x00AED581;   // 绿色=OK
    if (status.c_str() && status.len() > 4 &&
        status.c_str()[0] == 'E' && status.c_str()[1] == 'R')
        sc = 0x00EF9A9A;        // 红色=错误
    gfx::text(s, 6, H - 14, status.c_str(), sc, 0x0037474F);
}

} // namespace

// ---- 窗口胶水 ----
static DbMgr* dbm_of(Window* w) { return (DbMgr*)w->userdata; }

static void dbm_paint(Window* w) { dbm_of(w)->paint(w->back); }

static void dbm_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    DbMgr* m = dbm_of(w);
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }
    if (e->keycode == KEY_ENTER) { m->exec_input(); return; }
    if (e->keycode == KEY_BACKSPACE) {
        if (m->input_len > 0) { m->input_len--; m->input[m->input_len] = 0; }
        return;
    }
    // 可打印字符
    if (e->ascii >= 32 && e->ascii < 127 && m->input_len < INPUT_MAX - 1) {
        m->input[m->input_len++] = (char)e->ascii;
        m->input[m->input_len] = 0;
    }
}

static void dbm_tick(Window* w) {
    DbMgr* m = dbm_of(w);
    // 光标闪烁:每 ~400ms 翻转一次
    static uint32_t last = 0;
    uint32_t now = platform_tick_ms();
    if (now - last > 400) { m->cursor_on = !m->cursor_on; last = now; }
}

static void dbm_close(Window* w) {
    if (w->userdata) delete (DbMgr*)w->userdata;
    w->userdata = 0;
}

void dbmanager_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Database Manager", x, y, DBG_W, DBG_H);
    if (!w) return;
    DbMgr* m = new DbMgr();
    w->userdata = m;
    w->on_paint = dbm_paint;
    w->on_key = dbm_key;
    w->on_tick = dbm_tick;
    w->on_close = dbm_close;
    g_wm->raise(w);
}

} // namespace nefu
