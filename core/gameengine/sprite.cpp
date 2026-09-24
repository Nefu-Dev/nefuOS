// sprite.cpp —— 精灵系统纯逻辑：动画状态机推进 + 自测
#include "sprite.h"
#include <cstdio>

namespace nefu {
namespace gameengine {

// ============================================================================
//  Animator
// ============================================================================
void Animator::play(AnimationClip* clip, bool force) {
    if (!clip || clip->frame_count() == 0) return;
    if (!force && clip == current) {
        // 已在播放该剪辑：仅在暂停时恢复
        playing = true;
        return;
    }
    current = clip;
    reset();
    playing = true;
}

void Animator::update(int dt_ms) {
    if (!current || !playing) return;
    int n = current->frame_count();
    if (n <= 1) { finished = !current->loop; return; }

    timer_ms += dt_ms;
    int step = current->frame_ms > 0 ? current->frame_ms : 100;

    // 一帧可能跨过多帧（掉帧时追赶），用循环推进，避免一次跳太多
    while (timer_ms >= step) {
        timer_ms -= step;
        frame_index += direction;

        if (current->pingpong) {
            // 往返：越过端点时折返到相邻帧（序列 0,1,2,1,0,1,2,...）
            if (frame_index >= n) {
                direction = -1;
                frame_index = n >= 2 ? n - 2 : 0;
            } else if (frame_index < 0) {
                direction = 1;
                frame_index = n >= 2 ? 1 : 0;
            }
            // 非循环 pingpong：走到任一终点即结束
            if (!current->loop &&
                ((direction == -1 && frame_index == n - 2) ||
                 (direction == 1  && frame_index == 1))) {
                finished = true;
                playing = false;
            }
        } else {
            // 普通循环 / 单次
            if (frame_index >= n) {
                if (current->loop) {
                    frame_index = 0;
                } else {
                    frame_index = n - 1;
                    finished = true;
                    playing = false;
                }
            }
        }
    }
}

// ============================================================================
//  Sprite 世界 AABB（粗略）
// ============================================================================
void Sprite::world_aabb(const SpriteFrame* f, int& out_x, int& out_y,
                        int& out_w, int& out_h) const {
    if (!f) { out_x = out_y = out_w = out_h = 0; return; }
    int w = fx::fixtoi(fx::fx_mul(fx::itofix(f->sw), scale.x));
    int h = fx::fixtoi(fx::fx_mul(fx::itofix(f->sh), scale.y));
    out_w = w > 0 ? w : 1;
    out_h = h > 0 ? h : 1;
    out_x = position.to_ix() - f->ox;
    out_y = position.to_iy() - f->oy;
}

// ============================================================================
//  自测
// ============================================================================
int sprite_self_test() {
    int fails = 0;

    // 构造一个 4 帧的剪辑
    AnimationClip clip;
    clip.id = 1;
    clip.name = "run";
    clip.frame_ms = 100;     // 10fps
    clip.loop = true;
    clip.pingpong = false;
    for (int i = 0; i < 4; i++) {
        SpriteFrame f;
        f.sx = i * 16; f.sy = 0; f.sw = 16; f.sh = 16; f.ox = 8; f.oy = 8;
        clip.frames.push(f);
    }

    Animator a;
    a.play(&clip);
    if (!a.playing) fails++;
    if (a.frame_index != 0) fails++;

    // 推进 100ms -> 第 1 帧
    a.update(100);
    if (a.frame_index != 1) fails++;

    // 再推进 200ms（2 步）-> 第 4 帧（索引 3）
    a.update(200);
    if (a.frame_index != 3) fails++;

    // 循环：再推进 200ms 应回到索引 0（从 3 走 1 步到 0）
    a.update(100);
    if (a.frame_index != 0) fails++;

    // 帧指针有效
    const SpriteFrame* fr = a.frame();
    if (!fr) fails++;
    if (fr->sw != 16 || fr->sh != 16) fails++;

    // 单次播放（非循环）
    AnimationClip once;
    once.frame_ms = 50;
    once.loop = false;
    once.pingpong = false;
    for (int i = 0; i < 3; i++) {
        SpriteFrame f = { i*16, 0, 16, 16, 8, 8 };
        once.frames.push(f);
    }
    Animator b;
    b.play(&once);
    b.update(50);  // ->1
    b.update(50);  // ->2
    if (b.frame_index != 2) fails++;
    b.update(50);  // 越界 -> 停在 2，finished
    if (b.frame_index != 2) fails++;
    if (!b.finished) fails++;
    if (b.playing) fails++;

    // pingpong：往返
    AnimationClip pp;
    pp.frame_ms = 50;
    pp.loop = true;
    pp.pingpong = true;
    for (int i = 0; i < 3; i++) {
        SpriteFrame f = { i*16, 0, 16, 16, 8, 8 };
        pp.frames.push(f);
    }
    Animator c;
    c.play(&pp);
    c.update(50);  // ->1
    c.update(50);  // ->2
    if (c.frame_index != 2) fails++;
    c.update(50);  // 到末尾，反转到 1
    if (c.frame_index != 1) fails++;
    c.update(50);  // ->0
    c.update(50);  // 到开头，反转到 1
    if (c.frame_index != 1) fails++;

    // 切回同一剪辑（force=false）不应重置
    Animator d;
    d.play(&clip);
    d.update(250);  // 到帧 2
    int before = d.frame_index;
    d.play(&clip);   // 相同剪辑，不重置
    if (d.frame_index != before) fails++;

    // Sprite world AABB
    Sprite sp;
    sp.position = Vec2(fx::itofix(100), fx::itofix(100));
    sp.scale = Vec2(fx::itofix(2), fx::itofix(2));
    SpriteFrame sf = { 0, 0, 16, 16, 8, 8 };
    int ax, ay, aw, ah;
    sp.world_aabb(&sf, ax, ay, aw, ah);
    if (aw != 32 || ah != 32) fails++;       // 16*2
    if (ax != 100 - 8 || ay != 100 - 8) fails++;

    // SpriteSheet 切帧
    SpriteSheet sheet;
    sheet.tile_w = 16; sheet.tile_h = 16;
    sheet.cols = 4; sheet.rows = 2;
    if (sheet.frame_count() != 8) fails++;
    SpriteFrame t0 = sheet.tile_frame(0);
    if (t0.sx != 0 || t0.sy != 0) fails++;
    SpriteFrame t5 = sheet.tile_frame(5);
    if (t5.sx != 16 || t5.sy != 16) fails++;  // idx5: col1,row1


    // --- SpriteBatcher ---
    SpriteBatcher bat;
    bat.begin(0);
    bat.draw(fx::itofix(10), fx::itofix(10), 3);
    bat.draw(fx::itofix(20), fx::itofix(20), 1);
    bat.draw(fx::itofix(30), fx::itofix(30), 2);
    if (bat.size() != 3) fails++;
    bat.sort_by_frame();
    if (bat.entries[0].frame != 1) fails++;
    if (bat.entries[2].frame != 3) fails++;
    // 容量上限
    SpriteBatcher big;
    big.begin(0);
    bool ok = true;
    for (int i = 0; i < GE_BATCH_MAX + 10; i++)
        if (!big.draw(0, 0, i % 10)) ok = (i >= GE_BATCH_MAX);
    if (!ok) fails++;
    if (big.size() != GE_BATCH_MAX) fails++;

    // --- SpriteAtlas ---
    SpriteAtlas at;
    at.width = 128; at.height = 128;
    AtlasRect ra;
    at.pack(32, 32, 0, ra);
    at.pack(32, 32, 1, ra);
    at.pack(64, 16, 2, ra);
    if (at.count() != 3) fails++;
    // 超出宽度应失败
    AtlasRect rb;
    if (at.pack(200, 16, 3, rb)) fails++;

    // --- SpriteAnimator ---
    SpriteAnimator sa;
    sa.fps = 10;   // 每 100ms 一帧
    sa.play(0, 4);
    sa.update(50, 4);
    if (sa.current_frame != 0) fails++;
    sa.update(60, 4);   // 累计 110ms -> 第 1 帧
    if (sa.current_frame != 1) fails++;
    sa.loop = false;
    sa.update(400, 4);  // 到第 4 帧停
    if (sa.current_frame != 3) fails++;
    if (sa.playing) fails++;
    return fails;
}

} // namespace gameengine
} // namespace nefu

