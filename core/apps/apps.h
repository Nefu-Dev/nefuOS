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
    APP_WIKI,
    // system-level
    APP_SETTINGS,
    APP_STORE,
    // store app (installable/umount)
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
    APP_FONTVIEW,
    APP_EDITOR,
    APP_CALENDAR,
    APP_DISKUSAGE,
    APP_PASSGEN,
    APP_STICKY,
    APP_SCREENSHOT,
    APP_COLORPICKER,
    APP_SEARCH,
    APP_RECYCLEBIN,
    APP_WEATHER,
    APP_HELP,
    APP_DICTIONARY,
    APP_TASKMGR,
    APP_INPUTMETHOD,
    APP_CLIPBOARD,
    APP_NOTIFCENTER,
    APP_SHORTCUTS,
    APP_WALLPAPER,
    APP_THEME,
    APP_PROCESSLIST,
    APP_ABOUTFULL,
    APP_SYSINFOEXT,
    APP_CREDITS,
    APP_RELEASENOTES,
    APP_INSTALLER,
    APP_EMAIL,
    APP_GAMECENTER,
    APP_WIDGETS,
    APP_CLOCK_TIMER,
    APP_CONVERTER,
    // new batch: games
    APP_TETRIS,
    APP_GAME2048,
    APP_SUDOKU,
    APP_MEMORYMATCH,
    // new batch: tools
    APP_HEXEDIT,
    APP_JSONVIEW,
    APP_FINDFILES,
    APP_POMODORO,
    APP_STOPWATCH,
    APP_WORDCOUNT,
    // new batch: algorithm visualizer
    APP_ALGOVIZ,
    // new batch: graphics laboratory
    APP_GFXLAB,
    // new batch: simulation laboratory
    APP_SIMLAB,
#ifdef NEFU_LVGL_DEMO
    APP_LVGLDEMO,          // test-only build (build_host_only.ps1 -DNEFU_LVGL_DEMO)
#endif
    APP_VIDEOPLAYER,       // appended last so existing numeric ids stay stable
    APP_TEXTTOOL,          // text processing workbench
    APP_CRYPTOLAB,         // crypto lab
    APP_COMPRESSTOOL,      // compression tool
    APP_SERIALAB,          // serialization lab
    APP_AUDIOLAB,          // audio DSP lab
    APP_GFX3DVIEW,         // 3D viewer
    APP_MATHTOOL,          // math toolbox (mathext)
    APP_WIDGETGALLERY,     // UI widget gallery
    APP_DBMANAGER,         // database manager
    APP_BREAKOUT,          // breakout game
    APP_PONG,              // pong game
    APP_FLAPPY,            // flappy bird
    APP_SPACEINV,          // space invaders
    APP_PACMAN,            // pacman
    APP_TICTACTOE,         // tic-tac-toe
    APP_CONNECT4,          // connect four
    APP_MINESWEEPER2,      // advanced minesweeper
    APP_LIFE,              // game of life
    APP_MLLAB,             // machine learning lab
    APP_REPL,
    APP_SYSMON,
    APP_NETLAB,
    APP_FSVIEW,
    APP_RAYVIEW,
    APP_COMPILERLAB,
    APP_GEDEMO,
    APP_DEEPVIZ,
    APP_COUNT
};

// built-in (always available) app count: about all previous apps
#define APP_BUILTIN_COUNT (APP_SETTINGS)

const char* app_name(int id);
void app_launch(int id);
void app_weather_launch();
void app_help_launch();
void app_dictionary_launch();
void app_taskmgr_launch();
void app_launch(const char* name);  // resolve by app name (for autostart)
void app_show_textview(FSNode* file);
void app_show_image(FSNode* file);
bool decode_image_any(const uint8_t* data, uint32_t size, struct Surface& out);
void app_show_about();
void app_show_nefud(FSNode* file);
int  nefud_name_to_app_id(const char* name);
void calendar_launch();
void diskusage_launch();
void passgen_launch();
void sticky_launch();
void app_screenshot_launch();
void app_colorpicker_launch();
void app_search_launch();
void app_recyclebin_launch();

// install state (store management)
bool app_installed(int id);
// pre-install store apps shipped with the OS
void apps_preinstall_defaults();
void app_set_installed(int id, bool on);
// installed apps id list (excluding built-in), return count
int app_installed_list(int* ids, int max);
// store state persistence
void store_load();
void store_save();

// app launch (internal)
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
void video_player_launch();
void app_show_video(FSNode* file);
void video_player_autoplay(const char* host_path);   // test hook (--media)
void monitor_launch();
void browser_launch();
void browser_launch_url(const char* url);
void netcfg_launch();
void nefud_launch();
void fontview_launch();
void editor_launch();
void app_show_editor(FSNode* file);
void tetris_launch();
void game2048_launch();
void sudoku_launch();
void memorymatch_launch();
void hexedit_launch();
void jsonview_launch();
void findfiles_launch();
void pomodoro_launch();
void stopwatch_launch();
void wordcount_launch();
void algoviz_launch();
void gfxlab_launch();
void simlab_launch();
void texttool_launch();
void cryptolab_launch();
void compresstool_launch();
void serialab_launch();
void audiolab_launch();
void gfx3dview_launch();
void mathtool_launch();
void widgetgallery_launch();
void dbmanager_launch();
void breakout_launch();
void pong_launch();
void flappy_launch();
void spaceinv_launch();
void pacman_launch();
void tictactoe_launch();
void connect4_launch();
void minesweeper2_launch();
void life_launch();
void mllab_launch();
void repl_launch();
void sysmon_launch();
void netlab_launch();
void fsview_launch();
void rayview_launch();
void compilerlab_launch();
void gedemo_launch();
void deepviz_launch();
void wiki_launch();
void download_list_launch();
void lockscreen_launch();
#ifdef NEFU_LVGL_DEMO
void lvgl_demo_launch();
#endif

// tool: cascade window position
void cascade_pos(int* x, int* y);

// tool: node absolute path
String node_path(FSNode* n);

// tool: split file content by lines
void file_to_lines(FSNode* f, List<String>& out, int max_lines);

} // namespace nefu
