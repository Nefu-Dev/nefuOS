// nefuOS built-in apps
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
    // system-level
    APP_SETTINGS,
    APP_STORE,
    // store app（installable/umount）
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

// built-in（always available）app count：About all previous apps
#define APP_BUILTIN_COUNT (APP_SETTINGS)

const char* app_name(int id);
void app_launch(int id);
void app_show_textview(FSNode* file);
void app_show_image(FSNode* file);
void app_show_about();
void app_show_nefud(FSNode* file);
int  nefud_name_to_app_id(const char* name);

// install state（store management）
bool app_installed(int id);
// pre-install store apps shipped with the OS
void apps_preinstall_defaults();
void app_set_installed(int id, bool on);
// installed apps id list（excluding built-in），return count
int app_installed_list(int* ids, int max);
// store state persistence
void store_load();
void store_save();

// app launch（internal）
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

// tool：cascade window position
void cascade_pos(int* x, int* y);

// tool：node absolute path
String node_path(FSNode* n);

// tool：split file content by lines
void file_to_lines(FSNode* f, List<String>& out, int max_lines);

} // namespace nefu
