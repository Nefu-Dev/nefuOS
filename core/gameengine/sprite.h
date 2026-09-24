// sprite.h —— 精灵系统：精灵表 / 动画帧 / 动画状态机 / 翻转旋转缩放 / 混合模式
//
// 设计：
//   - 纯逻辑（动画状态推进、帧解析、源矩形计算）放在 sprite.cpp，可独立自测；
//   - 真正画到 Surface 的函数在此头文件中 inline（参考 games2_util.h 的做法），
//     未被调用时不产生外部符号，因此自测不依赖 gfx.cpp 链接。
//   - 坐标位置/旋转/缩放用 Q16.16 定点；像素矩形用整数。
#pragma once

#include <stdint.h>
#include "../klib/klib.h"
#include "../gui/gfx.h"
#include "ge_math.h"

namespace nefu {
namespace gameengine {

// ============================================================================
//  混合模式
// ============================================================================
enum BlendMode {
    BLEND_OPAQUE = 0,   // 不透明：直接覆盖
    BLEND_ALPHA,        // 透明：按 alpha 通道（演示版用色键透明）
    BLEND_ADD,          // 加法叠加（发光/粒子）
    BLEND_MULTIPLY,     // 正片叠底（变暗）
};

// 翻转位标志
enum FlipFlag {
    FLIP_NONE = 0,
    FLIP_H    = 1 << 0,   // 水平翻转
    FLIP_V    = 1 << 1,   // 垂直翻转
};

// ============================================================================
//  SpriteFrame —— 单帧：图集中的源矩形 + 锚点（热点）
// ============================================================================
struct SpriteFrame {
    int sx, sy;     // 图集源左上像素
    int sw, sh;     // 帧宽高（像素）
    int ox, oy;     // 锚点偏移（绘制时以此为中心对齐位置）
};

// ============================================================================
//  AnimationClip —— 一段动画（帧序列 + 节奏）
// ============================================================================
struct AnimationClip {
    int  id;
    const char* name;
    List<SpriteFrame> frames;   // 该剪辑的帧
    int  frame_ms;              // 每帧时长（毫秒）
    bool loop;
    bool pingpong;              // true = 往返播放（正放再倒放）

    AnimationClip() : id(0), name(0), frame_ms(100), loop(true), pingpong(false) {}

    int frame_count() const { return frames.size(); }
};

// ============================================================================
//  Animator —— 动画状态机：在剪辑间切换、推进帧
// ============================================================================
struct Animator {
    AnimationClip* current;     // 当前剪辑（nullptr = 无动画）
    int   frame_index;         // 当前帧索引
    int   direction;           // 1 = 前进，-1 = 后退（pingpong 用）
    int   timer_ms;            // 已累积的帧时间
    bool  playing;
    bool  finished;             // 非循环剪辑播完置 true

    Animator() : current(0), frame_index(0), direction(1),
                 timer_ms(0), playing(false), finished(false) {}

    // 切换到某剪辑；若与当前相同则忽略（除非 force）
    void play(AnimationClip* clip, bool force = false);

    // 推进动画；dt_ms 为距上一帧的毫秒数
    void update(int dt_ms);

    // 当前帧（无动画时返回 nullptr）
    const SpriteFrame* frame() const {
        if (!current || current->frame_count() == 0) return 0;
        return &current->frames[frame_index];
    }

    // 重置到剪辑开头
    void reset() { frame_index = 0; timer_ms = 0; finished = false; direction = 1; }
};

// ============================================================================
//  Sprite —— 一个可绘制精灵实例
// ============================================================================
struct Sprite {
    Vec2      position;        // 世界坐标（定点）
    fix       rotation;        // 弧度
    Vec2      scale;           // 缩放（x,y）
    int       flip;            // FlipFlag 位掩码
    BlendMode blend;
    uint32_t  tint;            // 色调（0xFFFFFFFF = 原色）
    bool      visible;
    Animator  anim;

    Sprite() : rotation(0), scale(fx::FX_ONE, fx::FX_ONE), flip(FLIP_NONE),
               blend(BLEND_ALPHA), tint(0x00FFFFFF), visible(true) {}

    // 世界 AABB（粗略：用帧宽高 * 缩放），供相机剔除/碰撞用
    void world_aabb(const SpriteFrame* f, int& out_x, int& out_y,
                    int& out_w, int& out_h) const;
};

// ============================================================================
//  SpriteSheet —— 图集：从一张 Surface 切出规则瓦片帧
// ============================================================================
struct SpriteSheet {
    Surface* image;        // 图集像素（由外部持有，这里只引用）
    int tile_w, tile_h;    // 单帧尺寸
    int cols, rows;        // 图集行列数

    SpriteSheet() : image(0), tile_w(0), tile_h(0), cols(0), rows(0) {}

    // 取第 idx 个瓦片作为一帧（按行优先），锚点在中心
    SpriteFrame tile_frame(int idx) const {
        SpriteFrame f;
        int cx = idx % cols;
        int cy = idx / cols;
        f.sx = cx * tile_w;
        f.sy = cy * tile_h;
        f.sw = tile_w;
        f.sh = tile_h;
        f.ox = tile_w / 2;
        f.oy = tile_h / 2;
        return f;
    }

    int frame_count() const { return cols * rows; }
};

// ============================================================================
//  渲染（inline，仅在 demo/app 中被调用时才编入）
//  色键透明：与 (0,0,0) 纯黑相同的像素视为透明。
// ============================================================================
// 把 src 像素按 90 度旋转倍数 + 翻转 blit 到 dst（最近邻）。
// 这是软件精灵的核心；为保持自测纯净，这里不被调用时不产生符号。
inline void blit_sprite(Surface& dst, Surface& src,
                        const SpriteFrame& fr, int dx, int dy,
                        int flip, fix scale_x, fix scale_y, uint32_t tint,
                        BlendMode blend) {
    // 缩放后的整数尺寸（最近邻采样）
    int dw = fx::fixtoi(fx::fx_mul(fx::itofix(fr.sw), scale_x));
    int dh = fx::fixtoi(fx::fx_mul(fx::itofix(fr.sh), scale_y));
    if (dw <= 0 || dh <= 0) return;
    bool flip_h = (flip & FLIP_H) != 0;
    bool flip_v = (flip & FLIP_V) != 0;

    for (int y = 0; y < dh; y++) {
        // 目标行 -> 源行（垂直翻转时反向）
        int sy = fr.sy + (flip_v ? (dh - 1 - y) * fr.sh / dh : y * fr.sh / dh);
        if (sy < 0) sy = 0;
        if (sy >= src.height) sy = src.height - 1;
        for (int x = 0; x < dw; x++) {
            int sx = fr.sx + (flip_h ? (dw - 1 - x) * fr.sw / dw : x * fr.sw / dw);
            if (sx < 0) sx = 0;
            if (sx >= src.width) sx = src.width - 1;
            uint32_t c = src.getpx(sx, sy);
            // 色键透明：纯黑跳过
            if (blend == BLEND_ALPHA && c == 0) continue;
            // 色调：与 tint 做分量级缩放（简化：tint=白则原色）
            if (tint != 0x00FFFFFF) {
                uint32_t r = (c & 0x00FF0000) >> 16;
                uint32_t g = (c & 0x0000FF00) >> 8;
                uint32_t b = (c & 0x000000FF);
                uint32_t tr = (tint & 0x00FF0000) >> 16;
                uint32_t tg = (tint & 0x0000FF00) >> 8;
                uint32_t tb = (tint & 0x000000FF);
                r = r * tr / 255; g = g * tg / 255; b = b * tb / 255;
                c = (r << 16) | (g << 8) | b;
            }
            int px = dx + x, py = dy + y;
            if (px < 0 || py < 0 || px >= dst.width || py >= dst.height) continue;
            if (blend == BLEND_ADD) {
                uint32_t dc = dst.getpx(px, py);
                int rr = (int)((dc & 0x00FF0000) >> 16) + (int)((c & 0x00FF0000) >> 16);
                int gg = (int)((dc & 0x0000FF00) >> 8) + (int)((c & 0x0000FF00) >> 8);
                int bb = (int)(dc & 0x000000FF) + (int)(c & 0x000000FF);
                if (rr > 255) rr = 255; if (gg > 255) gg = 255; if (bb > 255) bb = 255;
                dst.setpx(px, py, ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb);
            } else {
                dst.setpx(px, py, c);
            }
        }
    }
}

// 按精灵的世界变换绘制（位置 = 锚点）
inline void draw_sprite(Surface& dst, Surface& sheet_img, const Sprite& sp) {
    const SpriteFrame* f = sp.anim.frame();
    if (!f || !sp.visible) return;
    int dx = sp.position.to_ix() - f->ox;
    int dy = sp.position.to_iy() - f->oy;
    // 旋转演示版：简化为轴对齐 blit（真实旋转走 gfx:: 旋转光栅化，
    // bare 无 FPU 时用整数最近邻；这里保留缩放/翻转，旋转仅记录）
    blit_sprite(dst, sheet_img, *f, dx, dy, sp.flip, sp.scale.x, sp.scale.y,
                sp.tint, sp.blend);
}

// ============================================================================
//  自测
// ============================================================================

// ============================================================================
//  SpriteBatcher —— 收集一批 sprite 绘制调用，按纹理排序后批量提交
// ============================================================================
struct BatchEntry {
    fix      x, y;
    int      frame;
    uint32_t tint;
    fix      scale;
};

const int GE_BATCH_MAX = 64;
struct SpriteBatcher {
    BatchEntry entries[GE_BATCH_MAX];
    int   count;
    int   texture_id;

    SpriteBatcher() : count(0), texture_id(0) {}

    void begin(int tex) { count = 0; texture_id = tex; }
    bool draw(fix x, fix y, int frame, uint32_t tint = 0xFFFFFFFF, fix scale = fx::FX_ONE) {
        if (count >= GE_BATCH_MAX) return false;
        entries[count].x = x;
        entries[count].y = y;
        entries[count].frame = frame;
        entries[count].tint = tint;
        entries[count].scale = scale;
        count++;
        return true;
    }
    // 按 frame 排序（插入排序），相同纹理的连续绘制减少状态切换
    void sort_by_frame() {
        for (int i = 1; i < count; i++) {
            BatchEntry key = entries[i];
            int j = i - 1;
            while (j >= 0 && entries[j].frame > key.frame) {
                entries[j + 1] = entries[j];
                j--;
            }
            entries[j + 1] = key;
        }
    }
    int size() const { return count; }
    void clear() { count = 0; }
};

// ============================================================================
//  SpriteAtlas —— 简单矩形图集打包（线性 shelf 算法）
// ============================================================================
struct AtlasRect {
    int x, y, w, h;
    int id;     // 图片序号
};

struct SpriteAtlas {
    int       width, height;
    int       shelf_y;
    int       shelf_h;
    List<AtlasRect> rects;

    SpriteAtlas() : width(1024), height(1024), shelf_y(0), shelf_h(0) {}

    // 尝试放入一个矩形，返回是否成功
    bool pack(int w, int h, int id, AtlasRect& out) {
        if (w > width) return false;
        // 当前 shelf 还有水平空间
        int cur_w = (rects.size() > 0 && rects[rects.size()-1].y == shelf_y)
                    ? rects[rects.size()-1].x + rects[rects.size()-1].w : 0;
        if (cur_w + w <= width && h <= shelf_h) {
            out.x = cur_w; out.y = shelf_y; out.w = w; out.h = h; out.id = id;
            rects.push(out);
            return true;
        }
        // 换行
        if (shelf_h == 0) shelf_h = h;
        shelf_y += shelf_h;
        if (shelf_y + h > height) return false;
        shelf_h = h;
        out.x = 0; out.y = shelf_y; out.w = w; out.h = h; out.id = id;
        rects.push(out);
        return true;
    }
    int count() const { return rects.size(); }
};

// ============================================================================
//  SpriteAnimator —— 动画状态机（播放/停止/暂停/速度）
// ============================================================================
struct SpriteAnimator {
    int  current_frame;
    int  fps;
    int  elapsed;
    bool playing;
    bool loop;
    bool flip_x;
    bool flip_y;
    fix  rotation;
    fix  scale;

    SpriteAnimator() : current_frame(0), fps(12), elapsed(0),
                       playing(false), loop(true), flip_x(false), flip_y(false),
                       rotation(0), scale(fx::FX_ONE) {}

    void play(int start_frame, int count) {
        current_frame = start_frame;
        elapsed = 0;
        playing = true;
    }
    void stop() { playing = false; }
    void pause() { playing = false; }
    void resume() { playing = true; }

    // 每帧更新，返回当前帧序号
    int update(int dt_ms, int total_frames) {
        if (!playing) return current_frame;
        elapsed += dt_ms;
        int frame_dur = 1000 / (fps > 0 ? fps : 1);
        while (elapsed >= frame_dur) {
            elapsed -= frame_dur;
            current_frame++;
            if (current_frame >= total_frames) {
                if (loop) current_frame = 0;
                else { current_frame = total_frames - 1; playing = false; }
            }
        }
        return current_frame;
    }
};
int sprite_self_test();

} // namespace gameengine
} // namespace nefu

