// tilemap.h —— 瓦片地图：图层 / 瓦片集 / 碰撞层 / 视口相机 / 自动拼贴
//
// 设计：
//   - 纯逻辑（瓦片存储、碰撞查询、相机夹取、自动拼贴索引）放在 tilemap.cpp；
//   - 渲染到 Surface 的函数 inline 在头文件（未调用不产生符号）。
//   - Tiled 风格：每个图层是一个 int 瓦片索引网格；0 表示空瓦片。
#pragma once

#include <stdint.h>
#include "../klib/klib.h"
#include "../gui/gfx.h"
#include "ge_math.h"

namespace nefu {
namespace gameengine {

// ============================================================================
//  TileLayer —— 单个瓦片图层
// ============================================================================
struct TileLayer {
    int   width;          // 宽（瓦片数）
    int   height;         // 高（瓦片数）
    int*  tiles;         // 长度 width*height，瓦片索引（0=空）
    bool  visible;
    fix   parallax;       // 视差系数（1=与世界同速，<1 更远）

    TileLayer() : width(0), height(0), tiles(0), visible(true), parallax(fx::FX_ONE) {}

    // 分配网格（清零）。注意：用手动字节循环清零，规避 MinGW -O2 memset 误优化
    void alloc(int w, int h) {
        width = w; height = h;
        int n = w * h;
        if (tiles) delete[] tiles;
        tiles = new int[n > 0 ? n : 1];
        if (tiles) {
            for (int i = 0; i < n; i++) tiles[i] = 0;
        }
    }
    ~TileLayer() { if (tiles) delete[] tiles; tiles = 0; }

    // 取瓦片（越界返回 -1）
    int get(int tx, int ty) const {
        if (tx < 0 || ty < 0 || tx >= width || ty >= height) return -1;
        return tiles[ty * width + tx];
    }
    void set(int tx, int ty, int tile) {
        if (tx < 0 || ty < 0 || tx >= width || ty >= height) return;
        tiles[ty * width + tx] = tile;
    }
};

// ============================================================================
//  Tileset —— 瓦片集（一张图集切成等大瓦片）
// ============================================================================
struct Tileset {
    Surface* image;       // 图集（外部持有）
    int tile_w, tile_h;
    int first_id;         // 第一个瓦片的逻辑 id（Tiled 风格，通常 1）

    Tileset() : image(0), tile_w(16), tile_h(16), first_id(1) {}

    // 瓦片 id -> 图集像素坐标
    void id_to_xy(int id, int& out_x, int& out_y) const {
        int local = id - first_id;
        int cols = image ? (image->width / tile_w) : 1;
        out_x = (local % cols) * tile_w;
        out_y = (local / cols) * tile_h;
    }
};

// ============================================================================
//  Camera —— 视口/相机跟随
// ============================================================================
struct Camera {
    fix x, y;             // 相机左上角在世界中的坐标（定点）
    int view_w, view_h;   // 视口尺寸（屏幕像素）
    int map_w_px, map_h_px; // 地图总像素尺寸

    Camera() : x(0), y(0), view_w(320), view_h(240), map_w_px(0), map_h_px(0) {}

    // 把世界坐标转屏幕坐标
    int world_to_screen_x(fix wx) const { return fx::fixtoi(wx - x); }
    int world_to_screen_y(fix wy) const { return fx::fixtoi(wy - y); }

    // 把屏幕坐标转世界坐标
    fix screen_to_world_x(int sx) const { return x + fx::itofix(sx); }
    fix screen_to_world_y(int sy) const { return y + fx::itofix(sy); }

    // 夹取相机到地图边界（不允许看到地图外）
    void clamp_to_map() {
        if (map_w_px > view_w && x > fx::itofix(map_w_px - view_w))
            x = fx::itofix(map_w_px - view_w);
        if (map_h_px > view_h && y > fx::itofix(map_h_px - view_h))
            y = fx::itofix(map_h_px - view_h);
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        // 地图比视口小：居中
        if (map_w_px <= view_w) x = fx::itofix((view_w - map_w_px) / 2);
        if (map_h_px <= view_h) y = fx::itofix((view_h - map_h_px) / 2);
    }

    // 跟随目标（世界坐标），目标保持在视口中心
    void follow(fix target_x, fix target_y) {
        x = target_x - fx::itofix(view_w / 2);
        y = target_y - fx::itofix(view_h / 2);
        clamp_to_map();
    }
};

// ============================================================================
//  Tilemap —— 完整地图
// ============================================================================
struct Tilemap {
    List<TileLayer*> layers;
    Tileset*   tileset;
    TileLayer* collision;    // 碰撞层（可与某图层相同）
    int        tile_w, tile_h;
    int        width, height; // 瓦片数

    Tilemap() : tileset(0), collision(0), tile_w(16), tile_h(16),
                width(0), height(0) {}

    // 建一个空地图：自动生成一个视觉层 + 一个碰撞层
    void create(int w, int h, int tw, int th) {
        width = w; height = h; tile_w = tw; tile_h = th;
        TileLayer* visual = new TileLayer();
        visual->alloc(w, h);
        layers.push(visual);
        collision = new TileLayer();
        collision->alloc(w, h);
    }

    ~Tilemap() {
        for (int i = 0; i < layers.size(); i++) delete layers[i];
        layers.clear();
        if (collision) { delete collision; collision = 0; }
    }

    int map_pixel_w() const { return width * tile_w; }
    int map_pixel_h() const { return height * tile_h; }

    // 某瓦片是否实心（碰撞层 != 0）
    bool is_solid(int tx, int ty) const {
        if (!collision) return false;
        int v = collision->get(tx, ty);
        return v > 0;
    }

    // 世界像素坐标 -> 瓦片坐标
    void world_to_tile(fix wx, fix wy, int& out_tx, int& out_ty) const {
        out_tx = fx::fixtoi(fx::fx_div(wx, fx::itofix(tile_w)));
        out_ty = fx::fixtoi(fx::fx_div(wy, fx::itofix(tile_h)));
    }

    // 遍历一个 AABB（世界像素）覆盖到的所有实心瓦片，对每个调用回调。
    // 回调返回 true 时提前终止。
    template <typename F>
    void for_solid_in_aabb(fix x, fix y, fix w, fix h, F cb) const {
        fix tw = fx::itofix(tile_w);
        fix th = fx::itofix(tile_h);
        int tx0 = fx::fixtoi(fx::fx_div(x, tw));
        int ty0 = fx::fixtoi(fx::fx_div(y, th));
        int tx1 = fx::fixtoi(fx::fx_div(x + w, tw));
        int ty1 = fx::fixtoi(fx::fx_div(y + h, th));
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++) {
                if (is_solid(tx, ty)) {
                    if (cb(tx, ty)) return;
                }
            }
        }
    }
};

// ============================================================================
//  自动拼贴（autotile）：根据上下左右 4 邻居是否同组，选 16 宫格中的块
//  经典 2x2 角块方案的简化版：返回 0..15 的索引。
// ============================================================================
// 邻居掩码：bit0=上 bit1=右 bit2=下 bit3=左
// 返回应使用的自动瓦片变体索引（0..15）
int autotile_index(bool up, bool right, bool down, bool left);

// 计算一个瓦片网格里 (tx,ty) 的自动拼贴掩码
// solid_fn 判断邻居是否与 (tx,ty) 同组（连续地形）
int autotile_compute_mask(const Tilemap& map, int tx, int ty);

// ============================================================================
//  渲染（inline）：把可见瓦片 blit 到 Surface
// ============================================================================
inline void draw_tilemap(Surface& dst, const Tilemap& map, const Camera& cam) {
    if (!map.tileset) return;
    int tw = map.tile_w, th = map.tile_h;
    // 可见瓦片范围
    int tx0 = cam.world_to_screen_x(0) < 0 ?
              fx::fixtoi(-cam.x) / tw : 0;
    // 直接由相机坐标算可见范围
    int start_tx = fx::fixtoi(cam.x) / tw;
    int start_ty = fx::fixtoi(cam.y) / th;
    int end_tx = (fx::fixtoi(cam.x) + cam.view_w) / tw + 1;
    int end_ty = (fx::fixtoi(cam.y) + cam.view_h) / th + 1;
    if (start_tx < 0) start_tx = 0;
    if (start_ty < 0) start_ty = 0;

    for (int li = 0; li < map.layers.size(); li++) {
        TileLayer* L = map.layers[li];
        if (!L || !L->visible) continue;
        for (int ty = start_ty; ty <= end_ty && ty < L->height; ty++) {
            for (int tx = start_tx; tx <= end_tx && tx < L->width; tx++) {
                int tile = L->get(tx, ty);
                if (tile <= 0) continue;
                int sx, sy;
                map.tileset->id_to_xy(tile, sx, sy);
                int dx = tx * tw - fx::fixtoi(cam.x) -
                         (fx::fixtoi(cam.x) % tw) + (fx::fixtoi(cam.x) % tw);
                dx = tx * tw - fx::fixtoi(cam.x);
                int dy = ty * th - fx::fixtoi(cam.y);
                // 直接 blit 瓦片（无缩放）
                for (int py = 0; py < th; py++) {
                    for (int px = 0; px < tw; px++) {
                        uint32_t c = map.tileset->image->getpx(sx + px, sy + py);
                        int dxp = dx + px, dyp = dy + py;
                        if (dxp < 0 || dyp < 0 || dxp >= dst.width || dyp >= dst.height) continue;
                        if (c == 0) continue;   // 色键透明
                        dst.setpx(dxp, dyp, c);
                    }
                }
            }
        }
    }
}

// ============================================================================
//  自测
// ============================================================================

// ============================================================================
//  ParallaxLayer —— 视差背景层（以不同速度滚动）
// ============================================================================
struct ParallaxLayer {
    const char* name;
    int   tile_id;        // 用哪个瓦片填充
    fix   factor;         // 视差系数（0=固定，1=跟相机同速）
    int   scroll_x, scroll_y;
    uint32_t tint;

    ParallaxLayer() : name(0), tile_id(1), factor(fx::FX_HALF),
                      scroll_x(0), scroll_y(0), tint(0xFFFFFFFF) {}

    void update(int cam_x, int cam_y) {
        // 世界坐标 -> 层上的滚动 = 相机 * 系数（定点乘）
        scroll_x = fx::fixtoi(fx::fx_mul(fx::itofix(cam_x), factor));
        scroll_y = fx::fixtoi(fx::fx_mul(fx::itofix(cam_y), factor));
    }
};

// ============================================================================
//  TileCollision —— 瓦片地图 AABB 碰撞查询
// ============================================================================
struct TileCollision {
    const Tilemap* map;

    TileCollision() : map(0) {}

    // 判断世界坐标点是否在碰撞瓦片上
    bool solid_at(int wx, int wy) const {
        if (!map) return false;
        int tx = wx / map->tile_w;
        int ty = wy / map->tile_h;
        if (tx < 0 || ty < 0 || tx >= map->width || ty >= map->height) return true;
        return map->collision && map->collision->get(tx, ty) != 0;
    }
    // AABB 是否与任何固体瓦片重叠
    bool rect_hits(int x, int y, int w, int h) const {
        return solid_at(x, y) || solid_at(x + w, y) ||
               solid_at(x, y + h) || solid_at(x + w, y + h);
    }
};
int tilemap_self_test();

} // namespace gameengine
} // namespace nefu
