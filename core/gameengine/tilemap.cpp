// tilemap.cpp —— 瓦片地图纯逻辑：自动拼贴 + 碰撞 + 自测
#include "tilemap.h"

namespace nefu {
namespace gameengine {

// ============================================================================
//  自动拼贴：16 宫格变体
//  邻居掩码 bit0=上 bit1=右 bit2=下 bit3=左。
//  这里给出一个确定性的映射：根据四个方向是否连通，选 0..15 中的块。
// ============================================================================
int autotile_index(bool up, bool right, bool down, bool left) {
    int mask = 0;
    if (up)    mask |= 1 << 0;
    if (right) mask |= 1 << 1;
    if (down)  mask |= 1 << 2;
    if (left)  mask |= 1 << 3;
    return mask;   // 0..15 一一对应 4 位掩码
}

int autotile_compute_mask(const Tilemap& map, int tx, int ty) {
    int self = map.collision ? map.collision->get(tx, ty) : 0;
    if (self <= 0) return -1;   // 空地不参与拼贴
    bool up    = map.is_solid(tx, ty - 1);
    bool right = map.is_solid(tx + 1, ty);
    bool down  = map.is_solid(tx, ty + 1);
    bool left  = map.is_solid(tx - 1, ty);
    return autotile_index(up, right, down, left);
}

// ============================================================================
//  自测
// ============================================================================
int tilemap_self_test() {
    int fails = 0;

    // 建一个 10x8 的地图，瓦片 16x16
    Tilemap map;
    map.create(10, 8, 16, 16);
    if (map.width != 10 || map.height != 8) fails++;
    if (map.layers.size() != 1) fails++;
    if (map.map_pixel_w() != 160) fails++;
    if (map.map_pixel_h() != 128) fails++;

    // 默认全空
    if (map.is_solid(3, 3)) fails++;

    // 铺一行地面：y=7 的整行是实心
    for (int x = 0; x < 10; x++) {
        map.collision->set(x, 7, 1);
    }
    if (!map.is_solid(5, 7)) fails++;
    if (map.is_solid(5, 6)) fails++;

    // 世界坐标 -> 瓦片
    int tx, ty;
    map.world_to_tile(fx::itofix(40), fx::itofix(100), tx, ty);
    if (tx != 2 || ty != 6) fails++;   // 40/16=2, 100/16=6

    // for_solid_in_aabb：从 (0,100) 起 16x16 的 AABB 应碰到 y=7 的地面
    int hits = 0;
    map.for_solid_in_aabb(fx::itofix(0), fx::itofix(100),
                          fx::itofix(32), fx::itofix(16),
                          [&](int, int) { hits++; return false; });
    if (hits < 1) fails++;

    // 相机夹取：地图 160x128，视口 320x240 -> 应居中
    Camera cam;
    cam.view_w = 320; cam.view_h = 240;
    cam.map_w_px = map.map_pixel_w();
    cam.map_h_px = map.map_pixel_h();
    cam.follow(fx::itofix(80), fx::itofix(64));
    // 地图比视口小，x 应 = (320-160)/2 = 80 像素
    if (fx::fixtoi(cam.x) != 80) fails++;

    // 相机世界->屏幕->世界往返
    cam.x = 0; cam.y = 0;
    cam.map_w_px = 1000; cam.map_h_px = 1000;
    cam.clamp_to_map();
    if (cam.x != 0 || cam.y != 0) fails++;
    cam.x = fx::itofix(900);
    cam.clamp_to_map();
    // max = 1000-320 = 680
    if (fx::fixtoi(cam.x) != 680) fails++;

    // 自动拼贴掩码：角落块应只有 1 个邻居
    // 把 (2,2) 设实心，周围空 -> 掩码应为 0
    Tilemap m2;
    m2.create(8, 8, 16, 16);
    m2.collision->set(2, 2, 1);
    int mask0 = autotile_compute_mask(m2, 2, 2);
    if (mask0 != 0) fails++;

    // 右边再放一个 -> 掩码应含 right 位 (bit1=2)
    m2.collision->set(3, 2, 1);
    int maskR = autotile_compute_mask(m2, 2, 2);
    if (maskR != 2) fails++;

    // 上下左右都通 -> 掩码 = 1|2|4|8 = 15
    m2.collision->set(2, 1, 1);  // up
    m2.collision->set(2, 3, 1);  // down
    m2.collision->set(1, 2, 1);  // left
    int maskAll = autotile_compute_mask(m2, 2, 2);
    if (maskAll != 15) fails++;

    // autotile_index 直接映射
    if (autotile_index(true, false, false, false) != 1) fails++;
    if (autotile_index(false, false, false, true) != 8) fails++;


    // --- 视差层 ---
    ParallaxLayer pl;
    pl.factor = fx::FX_HALF;   // 0.5
    pl.update(1000, 200);
    if (pl.scroll_x != 500) fails++;
    if (pl.scroll_y != 100) fails++;
    ParallaxLayer pl2;
    pl2.factor = 0;   // 固定
    pl2.update(1000, 200);
    if (pl2.scroll_x != 0) fails++;

    // --- TileCollision ---
    Tilemap tm;
    tm.create(4, 4, 32, 32);
    tm.collision->set(1, 1, 1);   // (32..64, 32..64) 固体
    TileCollision tc; tc.map = &tm;
    if (!tc.solid_at(40, 40)) fails++;     // 在固体里
    if (tc.solid_at(0, 0)) fails++;        // 空

    return fails;
}

} // namespace gameengine
} // namespace nefu
