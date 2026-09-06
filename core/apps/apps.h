// nefuOS 内置应用
#pragma once
#include "../gui/wm.h"
#include "../vfs/vfs.h"

namespace nefu {

enum AppId {
    APP_FILEMGR = 0,
    APP_TERMINAL,
    APP_CALC,
    APP_TEXTVIEW,
    APP_SYSINFO,
    APP_ABOUT,
    // 系统级
    APP_SETTINGS,
    APP_STORE,
    // 商店应用（可安装/卸载）
    APP_SNAKE,
    APP_PAINT,
    APP_CLOCK,
    APP_NOTEPAD,
    APP_MINER,
    APP_IMAGEVIEWER,
    APP_MUSIC,
    APP_MONITOR,
    APP_BROWSER,
    APP_NETCFG,
    APP_NEFUD,
    APP_COUNT
};

// 内置（始终可用）应用数：About 之前的所有应用
#define APP_BUILTIN_COUNT (APP_SETTINGS)

const char* app_name(int id);
void app_launch(int id);
void app_show_textview(FSNode* file);
void app_show_image(FSNode* file);
void app_show_about();
void app_show_nefud(FSNode* file);
int  nefud_name_to_app_id(const char* name);

// 安装状态（软件商城管理）
bool app_installed(int id);
// pre-install store apps shipped with the OS
void apps_preinstall_defaults();
void app_set_installed(int id, bool on);
// 已安装应用 id 列表（不含内置），返回数量
int app_installed_list(int* ids, int max);
// 商店状态持久化
void store_load();
void store_save();

// 各应用启动（内部）
void fm_launch();
void term_launch();
void calc_launch();
void sysinfo_launch();
void settings_launch();
void store_launch();
void snake_launch();
void paint_launch();
void clock_launch();
void notepad_launch();
void miner_launch();
void imageviewer_launch();
void music_launch();
void monitor_launch();
void browser_launch();
void netcfg_launch();
void nefud_launch();

// 工具：级联窗口位置
void cascade_pos(int* x, int* y);

// 工具：节点绝对路径
String node_path(FSNode* n);

// 工具：将文件内容按行拆分
void file_to_lines(FSNode* f, List<String>& out, int max_lines);

} // namespace nefu
