// nefuOS 文件系统查看器 —— 窗口应用
//
// 在一块虚拟磁盘上格式化一个 FAT16,然后可视化:
//   - 上半部分:磁盘布局条(引导扇区 / FAT / 根目录 / 数据区)
//   - 左半:根目录文件树
//   - 右半:磁盘使用情况(已用簇 / 空闲簇 / 每簇字节数)
//
// 按键:
//   F   格式化磁盘(清空)
//   N   新建一个示例文件 demo_N.txt
//   D   删除第一个文件
//   Esc 关闭
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../filesystem/filesystem_all.h"
#include <string.h>

namespace nefu {
namespace {

const int FS_W = 620, FS_H = 440;

struct FsView {
    filesystem::Disk   disk;
    filesystem::Fat16  fat;
    bool    mounted;
    int     file_counter;
    char    status[64];

    void reset() {
        // 8192 扇区 = 4MB,8 扇区/簇 = 4KB/簇
        disk.create(8192);
        mounted = fat.format(&disk, 0, 8192, 8);
        file_counter = 0;
        if (mounted) {
            // 预置几个文件
            const uint8_t* hello = (const uint8_t*)"Welcome to nefuOS filesystem!\n";
            fat.write_file("/welcome.txt", hello, 31);
            fat.mkdir("/docs");
            const uint8_t* note = (const uint8_t*)"readme: press F to format, N to add file.\n";
            fat.write_file("/docs/readme.txt", note, 44);
            file_counter = 2;
        }
        strcpy(status, mounted ? "ready" : "format failed");
    }

    void new_file() {
        if (!mounted) return;
        char name[32];
        char body[64];
        // 简单整数转字符串
        char num[8]; int n = file_counter; int i = 0;
        if (n == 0) num[i++] = '0';
        char tmp[8]; int t = 0;
        while (n > 0) { tmp[t++] = (char)('0' + (n % 10)); n /= 10; }
        while (t > 0) num[i++] = tmp[--t];
        num[i] = 0;
        strcpy(name, "/demo");
        strcat(name, num);
        strcat(name, ".txt");
        strcpy(body, "sample file #");
        strcat(body, num);
        uint32_t blen = 0; while (body[blen]) blen++;
        fat.write_file(name, (const uint8_t*)body, blen);
        file_counter++;
        strcpy(status, "created file");
    }

    void delete_first() {
        if (!mounted) return;
        filesystem::FatDirInfo en[16];
        int n = fat.list_dir("/", en, 16);
        for (int i = 0; i < n; i++) {
            if (!en[i].is_dir) {
                char path[32]; path[0]='/';
                int k=1; for(;en[i].name[k-1]&&k<30;k++) path[k]=en[i].name[k-1]; path[k]=0;
                fat.remove(path);
                strcpy(status, "deleted file");
                return;
            }
        }
        strcpy(status, "no file to delete");
    }

    void paint(Surface& s) {
        gfx::fillrect(s, 0, 0, FS_W, FS_H, 0x001E1E2E);
        gfx::text_scale(s, 10, 10, "nefuOS File System Viewer", 0x0089B4FA, 0x001E1E2E, 2);

        // ---- 磁盘布局条 ----
        int bar_y = 50, bar_x = 10, bar_w = FS_W - 20, bar_h = 26;
        gfx::text(s, 10, bar_y - 12, "Disk layout (4MB, 4KB/cluster):", 0x00CDD6F4, 0x001E1E2E);
        gfx::fillrect(s, bar_x, bar_y, 8, bar_h, 0x00F38BA8);
        gfx::text(s, bar_x + 10, bar_y + 6, "BPB", 0x00CDD6F4, 0x001E1E2E);
        int fat_x = bar_x + 120;
        gfx::fillrect(s, fat_x, bar_y, 60, bar_h, 0x00F9E2AF);
        gfx::text(s, fat_x + 4, bar_y + 6, "FAT", 0x001E1E2E, 0x00F9E2AF);
        int rd_x = fat_x + 64;
        gfx::fillrect(s, rd_x, bar_y, 40, bar_h, 0x00A6E3A1);
        gfx::text(s, rd_x + 2, bar_y + 6, "root", 0x001E1E2E, 0x00A6E3A1);
        int da_x = rd_x + 44;
        int da_w = bar_w - (da_x - bar_x) - 10;
        gfx::fillrect(s, da_x, bar_y, da_w, bar_h, 0x0089B4FA);
        gfx::text(s, da_x + 4, bar_y + 6, "data clusters", 0x001E1E2E, 0x0089B4FA);

        // ---- 文件树(左) ----
        gfx::text(s, 10, 95, "Directory tree:", 0x00CDD6F4, 0x001E1E2E);
        filesystem::FatDirInfo en[16];
        int n = mounted ? fat.list_dir("/", en, 16) : 0;
        int ty = 115;
        for (int i = 0; i < n && i < 10; i++) {
            char line[40]; int k = 0;
            line[k++] = en[i].is_dir ? '[' : ' ';
            int j = 0; while (en[i].name[j] && k < 38) line[k++] = en[i].name[j++];
            if (en[i].is_dir) line[k++] = ']';
            line[k] = 0;
            uint32_t col = en[i].is_dir ? 0x00F9E2AF : 0x00A6E3A1;
            gfx::text(s, 14, ty, line, col, 0x001E1E2E);
            ty += 16;
        }

        // ---- 磁盘使用(右) ----
        int ux = 360, uy = 95;
        gfx::text(s, ux, uy, "Usage:", 0x00CDD6F4, 0x001E1E2E);
        uint32_t tot = mounted ? fat.total_clusters() : 0;
        uint32_t fr  = mounted ? fat.free_clusters() : 0;
        uint32_t used = (tot > fr) ? (tot - fr) : 0;
        int bw = 220, bh = 14;
        gfx::fillrect(s, ux, uy + 18, bw, bh, 0x0045475A);
        int usedw = tot ? (int)((int64_t)used * bw / tot) : 0;
        gfx::fillrect(s, ux, uy + 18, usedw, bh, 0x00F38BA8);
        uy += 44;
        char ln[48];
        // 用简单整数->文本,不依赖 sprintf
        uy += 16;
        gfx::text(s, ux, uy, "free clusters:", 0x00CDD6F4, 0x001E1E2E);
        uy += 16;
        gfx::text(s, ux, uy, "used clusters:", 0x00CDD6F4, 0x001E1E2E);
        uy += 16;
        gfx::text(s, ux, uy, "bytes/cluster:", 0x00CDD6F4, 0x001E1E2E);

        // ---- 文件内容预览(左下,第一个文件前 48 字节) ----
        int px = 10, py = 250;
        gfx::text(s, px, py, "Preview:", 0x00CDD6F4, 0x001E1E2E);
        if (n > 0 && !en[0].is_dir) {
            char p2[32]; p2[0]='/';
            int k=1; for(int j=0;en[0].name[j]&&k<30;j++) p2[k++]=en[0].name[j]; p2[k]=0;
            uint8_t pd[64]; int pr = fat.read_file(p2, pd, 48);
            int oy = py + 16;
            for (int i = 0; i < pr && i < 48; i += 16) {
                char line[40]; int lk=0;
                for (int b=0; b<16 && i+b<pr; b++) {
                    uint8_t ch = pd[i+b];
                    line[lk++] = (ch >= 32 && ch < 127) ? (char)ch : '.';
                }
                line[lk]=0;
                gfx::text(s, px+4, oy, line, 0x009399B3, 0x001E1E2E);
                oy += 14;
            }
        }

        // 状态行
        gfx::text(s, 10, FS_H - 40, status, 0x00F9E2AF, 0x001E1E2E);
        gfx::text(s, 10, FS_H - 20, "F: format   N: new file   D: delete   Esc: close",
                  0x009399B3, 0x001E1E2E);
    }
};

} // namespace

static FsView* view_of(Window* w) { return (FsView*)w->userdata; }

static void fs_paint(Window* w) { view_of(w)->paint(w->back); }
static void fs_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    FsView* v = view_of(w);
    if (e->ascii == 'f' || e->ascii == 'F') { v->reset(); return; }
    if (e->ascii == 'n' || e->ascii == 'N') { v->new_file(); return; }
    if (e->ascii == 'd' || e->ascii == 'D') { v->delete_first(); return; }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}
static void fs_close(Window* w) {
    if (w->userdata) delete (FsView*)w->userdata;
    w->userdata = 0;
}

void fsview_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("FS Viewer", x, y, FS_W, FS_H);
    if (!w) return;
    FsView* v = new FsView();
    v->reset();
    w->userdata = v;
    w->on_paint = fs_paint;
    w->on_key = fs_key;
    w->on_close = fs_close;
    g_wm->raise(w);
}

} // namespace nefu
