// ============================================================================
// nefuOS 3D 图形库 —— raster 实现（软件光栅化核心）
// ============================================================================
#include "raster.h"

#include <cmath>
#include <cstring>

namespace nefu {
namespace gfx3d {

static inline double f_min(double a, double b) { return a < b ? a : b; }
static inline double f_max(double a, double b) { return a > b ? a : b; }
static inline double f_abs(double x) { return x < 0 ? -x : x; }

// ============================================================================
// 颜色工具
// ============================================================================
uint32_t pack_color(double r, double g, double b, double a) {
    if (r < 0) r = 0; if (r > 1) r = 1;
    if (g < 0) g = 0; if (g > 1) g = 1;
    if (b < 0) b = 0; if (b > 1) b = 1;
    if (a < 0) a = 0; if (a > 1) a = 1;
    uint32_t R = (uint32_t)(r * 255.0);
    uint32_t G = (uint32_t)(g * 255.0);
    uint32_t B = (uint32_t)(b * 255.0);
    uint32_t A = (uint32_t)(a * 255.0);
    return (A << 24) | (R << 16) | (G << 8) | B;
}

void unpack_color(uint32_t c, double& r, double& g, double& b) {
    r = ((c >> 16) & 0xFF) / 255.0;
    g = ((c >> 8) & 0xFF) / 255.0;
    b = (c & 0xFF) / 255.0;
}

uint32_t alpha_blend(uint32_t src, uint32_t dst) {
    double sr, sg, sb, dr, dg, db;
    unpack_color(src, sr, sg, sb);
    unpack_color(dst, dr, dg, db);
    double a = ((src >> 24) & 0xFF) / 255.0;
    return pack_color(sr * a + dr * (1 - a),
                      sg * a + dg * (1 - a),
                      sb * a + db * (1 - a));
}

// ============================================================================
// 纹理采样
// ============================================================================
static inline uint32_t texel(const uint32_t* tex, int tw, int th, int x, int y) {
    if (x < 0) x = 0; if (x >= tw) x = tw - 1;
    if (y < 0) y = 0; if (y >= th) y = th - 1;
    return tex[y * tw + x];
}

uint32_t sample_texture_nearest(const uint32_t* tex, int tw, int th, double u, double v) {
    if (!tex) return 0xFFFFFFFF;
    // u,v 归一化到 [0,1]
    u = u - (int)u; if (u < 0) u += 1;
    v = v - (int)v; if (v < 0) v += 1;
    int x = (int)(u * (tw - 1) + 0.5);
    int y = (int)(v * (th - 1) + 0.5);
    return texel(tex, tw, th, x, y);
}

uint32_t sample_texture_bilinear(const uint32_t* tex, int tw, int th, double u, double v) {
    if (!tex) return 0xFFFFFFFF;
    u = u - (int)u; if (u < 0) u += 1;
    v = v - (int)v; if (v < 0) v += 1;
    double fx = u * (tw - 1);
    double fy = v * (th - 1);
    int x0 = (int)fx, y0 = (int)fy;
    double tx = fx - x0, ty = fy - y0;
    uint32_t c00 = texel(tex, tw, th, x0,     y0);
    uint32_t c10 = texel(tex, tw, th, x0 + 1, y0);
    uint32_t c01 = texel(tex, tw, th, x0,     y0 + 1);
    uint32_t c11 = texel(tex, tw, th, x0 + 1, y0 + 1);
    double r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
    unpack_color(c00, r00, g00, b00);
    unpack_color(c10, r10, g10, b10);
    unpack_color(c01, r01, g01, b01);
    unpack_color(c11, r11, g11, b11);
    double r = (r00 * (1 - tx) + r10 * tx) * (1 - ty) + (r01 * (1 - tx) + r11 * tx) * ty;
    double g = (g00 * (1 - tx) + g10 * tx) * (1 - ty) + (g01 * (1 - tx) + g11 * tx) * ty;
    double b = (b00 * (1 - tx) + b10 * tx) * (1 - ty) + (b01 * (1 - tx) + b11 * tx) * ty;
    return pack_color(r, g, b);
}

// ============================================================================
// Liang-Barsky 线段裁剪
// ============================================================================
bool clip_line_liang_barsky(int& x0, int& y0, int& x1, int& y1,
                            int xmin, int ymin, int xmax, int ymax) {
    double dx = x1 - x0, dy = y1 - y0;
    double t0 = 0, t1 = 1;
    double p[4] = { -dx, dx, -dy, dy };
    double q[4] = { x0 - xmin, xmax - x0, y0 - ymin, ymax - y0 };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) return false;     // 平行且在窗外
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0) { if (t > t0) t0 = t; }
            else          { if (t < t1) t1 = t; }
        }
    }
    if (t0 > t1) return false;
    int nx0 = (int)(x0 + t0 * dx), ny0 = (int)(y0 + t0 * dy);
    int nx1 = (int)(x0 + t1 * dx), ny1 = (int)(y0 + t1 * dy);
    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
    return true;
}

// ============================================================================
// Rasterizer 生命周期
// ============================================================================
void Rasterizer::attach(gfxlib::Buffer buf) {
    target = buf;
    w = buf.w; h = buf.h;
    viewport_x = 0; viewport_y = 0;
    viewport_w = (double)w; viewport_h = (double)h;
    detach();
    zbuf = new float[(size_t)w * (size_t)h];
    clear_depth();
}

void Rasterizer::detach() {
    if (zbuf) { delete[] zbuf; zbuf = 0; }
}

void Rasterizer::clear(uint32_t color) {
    if (!target.data) return;
    for (int i = 0; i < w * h; i++) target.data[i] = color;
    clear_depth();
}

void Rasterizer::clear_depth() {
    if (!zbuf) return;
    for (int i = 0; i < w * h; i++) zbuf[i] = 1e30f;
}

// ============================================================================
// 视锥裁剪：Sutherland-Hodgman 对 clip space 6 平面
// 平面：-w <= x <= w, -w <= y <= w, -w <= z <= w
// 输出为一个凸多边形（顶点数 <= 9），外部三角扇化。
// ============================================================================
struct ClipPoly {
    Rasterizer::ClipVert v[16];
    int n;
};

// 与一个半空间相交：f(v) = sign * (分量 ± w) >= 0
// axis: 0=x,1=y,2=z; sign: +1 表示 w-component >= 0（裁掉分量<-w）, -1 表示 component<=w
static void clip_against(ClipPoly& inout, int axis, int sign) {
    ClipPoly out;
    out.n = 0;
    if (inout.n == 0) return;
    for (int i = 0; i < inout.n; i++) {
        const Rasterizer::ClipVert& cur = inout.v[i];
        const Rasterizer::ClipVert& prv = inout.v[(i + inout.n - 1) % inout.n];
        double cv = (axis == 0) ? cur.clip.x : (axis == 1) ? cur.clip.y : cur.clip.z;
        double pv = (axis == 0) ? prv.clip.x : (axis == 1) ? prv.clip.y : prv.clip.z;
        double cw = cur.clip.w, pw = prv.clip.w;
        // inside(sign=+1): cv + cw >= 0; inside(sign=-1): cw - cv >= 0
        double cur_in  = (sign > 0) ? (cv + cw) : (cw - cv);
        double prv_in  = (sign > 0) ? (pv + pw) : (pw - pv);
        bool cin = cur_in >= -1e-12;
        bool pin = prv_in >= -1e-12;
        if (cin) {
            if (!pin) {
                // 求交点：t = prv_in / (prv_in - cur_in)
                double t = prv_in / (prv_in - cur_in);
                Rasterizer::ClipVert iv;
                iv.clip  = prv.clip  + (cur.clip  - prv.clip)  * t;
                iv.world = prv.world + (cur.world - prv.world) * t;
                iv.normal= prv.normal+ (cur.normal-prv.normal)* t;
                iv.uv    = prv.uv    + (cur.uv    - prv.uv)    * t;
                iv.color = prv.color + (cur.color - prv.color) * t;
                out.v[out.n++] = iv;
            }
            out.v[out.n++] = cur;
        } else if (pin) {
            double t = prv_in / (prv_in - cur_in);
            Rasterizer::ClipVert iv;
            iv.clip  = prv.clip  + (cur.clip  - prv.clip)  * t;
            iv.world = prv.world + (cur.world - prv.world) * t;
            iv.normal= prv.normal+ (cur.normal-prv.normal)* t;
            iv.uv    = prv.uv    + (cur.uv    - prv.uv)    * t;
            iv.color = prv.color + (cur.color - prv.color) * t;
            out.v[out.n++] = iv;
        }
    }
    inout = out;
}

// 透视除法 + 视口变换：把 clip 顶点转成屏幕像素坐标
struct ScreenVert {
    double sx, sy;      // 屏幕像素中心坐标
    double inv_w;       // 1 / clip.w
    Vec3 world_over_w;  // world / w
    Vec3 norm_over_w;
    Vec2 uv_over_w;
    Vec4 col_over_w;
    double depth;       // NDC z in [-1,1]
};

static ScreenVert to_screen(const Rasterizer& r, const Rasterizer::ClipVert& v) {
    ScreenVert s;
    double w = v.clip.w;
    if (f_abs(w) < 1e-12) w = 1e-12;
    double invw = 1.0 / w;
    s.inv_w = invw;
    double ndc_x = v.clip.x * invw;
    double ndc_y = v.clip.y * invw;
    double ndc_z = v.clip.z * invw;
    // NDC [-1,1] -> 屏幕 [viewport_x, viewport_x+viewport_w]
    s.sx = r.viewport_x + (ndc_x * 0.5 + 0.5) * r.viewport_w;
    s.sy = r.viewport_y + (1.0 - (ndc_y * 0.5 + 0.5)) * r.viewport_h;   // y 翻转
    s.depth = ndc_z;
    s.world_over_w  = v.world  * invw;
    s.norm_over_w   = v.normal* invw;
    s.uv_over_w     = v.uv    * invw;
    s.col_over_w    = v.color * invw;
    return s;
}

// ============================================================================
// 光栅化一个屏幕空间三角形（三个 ScreenVert）
// ============================================================================
static void raster_screen_tri(Rasterizer& r,
                              const ScreenVert& s0,
                              const ScreenVert& s1,
                              const ScreenVert& s2) {
    // 包围盒
    double minx = f_min(f_min(s0.sx, s1.sx), s2.sx);
    double maxx = f_max(f_max(s0.sx, s1.sx), s2.sx);
    double miny = f_min(f_min(s0.sy, s1.sy), s2.sy);
    double maxy = f_max(f_max(s0.sy, s1.sy), s2.sy);
    int x0 = (int)floor(minx), x1 = (int)ceil(maxx);
    int y0 = (int)floor(miny), y1 = (int)ceil(maxy);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= r.w) x1 = r.w - 1; if (y1 >= r.h) y1 = r.h - 1;

    // 背面剔除（屏幕绕序：叉积 z < 0 为 CCW，这里约定 CW 正面，可配置）
    double dw = (s1.sy - s2.sy) * (s0.sx - s2.sx) + (s2.sx - s1.sx) * (s0.sy - s2.sy);
    if (r.backface_cull && f_abs(dw) < 1e-9) return;
    // 约定：dw > 0 为正面（CW），剔除 dw < 0
    if (r.backface_cull && dw < 0) return;

    double inv_dw = 1.0 / dw;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            double px = x + 0.5, py = y + 0.5;
            // 重心坐标
            double a = ((s1.sy - s2.sy) * (px - s2.sx) + (s2.sx - s1.sx) * (py - s2.sy)) * inv_dw;
            double b = ((s2.sy - s0.sy) * (px - s2.sx) + (s0.sx - s2.sx) * (py - s2.sy)) * inv_dw;
            double c = 1.0 - a - b;
            if (a < -1e-6 || b < -1e-6 || c < -1e-6) continue;

            // 透视校正：插值 attr/w 与 1/w，再相除
            double inv_w = a * s0.inv_w + b * s1.inv_w + c * s2.inv_w;
            if (f_abs(inv_w) < 1e-9) continue;
            double ow = 1.0 / inv_w;

            FragInput in;
            Vec3 ww = s0.world_over_w * a + s1.world_over_w * b + s2.world_over_w * c;
            Vec3 nn = s0.norm_over_w  * a + s1.norm_over_w  * b + s2.norm_over_w  * c;
            Vec2 uv = s0.uv_over_w    * a + s1.uv_over_w    * b + s2.uv_over_w    * c;
            Vec4 cc = s0.col_over_w   * a + s1.col_over_w   * b + s2.col_over_w   * c;
            in.world  = ww * ow;
            in.normal = nn * ow;
            in.uv     = uv * ow;
            in.color  = cc * ow;
            double depth = (a * s0.depth + b * s1.depth + c * s2.depth) * ow;
            in.depth = (depth + 1.0) * 0.5;   // NDC [-1,1] -> [0,1]

            // 深度测试
            int idx = y * r.w + x;
            if (in.depth > r.zbuf[idx]) continue;
            r.zbuf[idx] = (float)in.depth;

            uint32_t color = 0xFFFFFFFF;
            if (r.frag_shader) r.frag_shader(in, r.frag_user, color);
            r.target.data[idx] = color;
        }
    }
}

// ============================================================================
// draw_triangle：裁剪 -> 三角扇 -> 光栅化
// ============================================================================
void Rasterizer::draw_triangle(const ClipVert& v0, const ClipVert& v1, const ClipVert& v2) {
    ClipPoly poly;
    poly.n = 0;
    poly.v[poly.n++] = v0;
    poly.v[poly.n++] = v1;
    poly.v[poly.n++] = v2;

    // 6 个裁剪面：x=±w, y=±w, z=±w
    clip_against(poly, 0, +1);   // x >= -w
    clip_against(poly, 0, -1);   // x <=  w
    clip_against(poly, 1, +1);   // y >= -w
    clip_against(poly, 1, -1);   // y <=  w
    clip_against(poly, 2, +1);   // z >= -w
    clip_against(poly, 2, -1);   // z <=  w

    if (poly.n < 3) return;

    // 三角扇化 (0, i, i+1)
    ScreenVert s[16];
    for (int i = 0; i < poly.n; i++) s[i] = to_screen(*this, poly.v[i]);
    for (int i = 1; i + 1 < poly.n; i++) {
        raster_screen_tri(*this, s[0], s[i], s[i + 1]);
    }
}

// ============================================================================
// 屏幕空间线段 / 点
// ============================================================================
void Rasterizer::draw_line_screen(int x0, int y0, int x1, int y1, uint32_t color) {
    if (!target.data) return;
    if (!clip_line_liang_barsky(x0, y0, x1, y1, 0, 0, w - 1, h - 1)) return;
    gfxlib::draw_line(target, x0, y0, x1, y1, color);
}

void Rasterizer::draw_point(int x, int y, uint32_t color) {
    if (!target.data) return;
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    target.data[y * w + x] = color;
}

// ============================================================================
// mipmap 与三线性采样
// ============================================================================
void mipmap_generate(const uint32_t* src, int w, int h,
                     uint32_t** levels_out, int* lw_out, int* lh_out, int max_levels) {
    const uint32_t* cur = src;
    int cw = w, ch = h;
    int lvl = 0;
    while (cw >= 1 && ch >= 1 && lvl < max_levels) {
        lw_out[lvl] = cw;
        lh_out[lvl] = ch;
        levels_out[lvl] = new uint32_t[(size_t)cw * ch];
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++)
                levels_out[lvl][y * cw + x] = cur[y * cw + x];
        if (cw == 1 && ch == 1) break;
        // 生成下一级：2x2 平均
        int nw = cw > 1 ? cw / 2 : 1;
        int nh = ch > 1 ? ch / 2 : 1;
        uint32_t* next = new uint32_t[(size_t)nw * nh];
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                uint32_t c00 = cur[(2*y) * cw + (2*x)];
                uint32_t c10 = cur[(2*y) * cw + (2*x + (2*x+1 < cw ? 1 : 0))];
                uint32_t c01 = cur[(2*y + (2*y+1 < ch ? 1 : 0)) * cw + (2*x)];
                uint32_t c11 = cur[(2*y + (2*y+1 < ch ? 1 : 0)) * cw + (2*x + (2*x+1 < cw ? 1 : 0))];
                double r = (((c00>>16)&0xFF)+((c10>>16)&0xFF)+((c01>>16)&0xFF)+((c11>>16)&0xFF))/4.0;
                double g = (((c00>>8)&0xFF)+((c10>>8)&0xFF)+((c01>>8)&0xFF)+((c11>>8)&0xFF))/4.0;
                double b = ((c00&0xFF)+(c10&0xFF)+(c01&0xFF)+(c11&0xFF))/4.0;
                next[y*nw+x] = pack_color(r/255.0, g/255.0, b/255.0);
            }
        cur = next; cw = nw; ch = nh;
        lvl++;
    }
}

uint32_t sample_texture_trilinear(const uint32_t* const* levels,
                                 const int* lw, const int* lh, int levels_count,
                                 double u, double v, double lod) {
    if (levels_count <= 0) return 0xFFFFFFFF;
    if (lod < 0) lod = 0;
    if (lod > levels_count - 1.001) lod = levels_count - 1.001;
    int l0 = (int)lod;
    double f = lod - l0;
    uint32_t c0 = sample_texture_bilinear(levels[l0], lw[l0], lh[l0], u, v);
    uint32_t c1 = (l0 + 1 < levels_count)
        ? sample_texture_bilinear(levels[l0+1], lw[l0+1], lh[l0+1], u, v)
        : c0;
    // 按 alpha 混合两个颜色
    double r0,g0,b0, r1,g1,b1;
    unpack_color(c0, r0, g0, b0);
    unpack_color(c1, r1, g1, b1);
    return pack_color(r0*(1-f)+r1*f, g0*(1-f)+g1*f, b0*(1-f)+b1*f);
}

// ============================================================================
// 扫描线光栅化（备用教学实现）
// ============================================================================
static double edge_sign(double x0, double y0, double x1, double y1, double x, double y) {
    return (x - x0) * (y1 - y0) - (y - y0) * (x1 - x0);
}

void raster_scanline(Rasterizer& r,
                     double x0, double y0, double x1, double y1, double x2, double y2) {
    // 简单实现：按包围盒逐像素，重心坐标判断（与 bbox 法相同，但单独成函数供教学）
    int miny = (int)f_min(f_min(y0, y1), y2);
    int maxy = (int)f_max(f_max(y0, y1), y2);
    int minx = (int)f_min(f_min(x0, x1), x2);
    int maxx = (int)f_max(f_max(x0, x1), x2);
    if (minx < 0) minx = 0; if (miny < 0) miny = 0;
    if (maxx >= r.w) maxx = r.w - 1;
    if (maxy >= r.h) maxy = r.h - 1;
    double area = edge_sign(x0,y0,x1,y1,x2,y2);
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++) {
            double w0 = edge_sign(x1,y1,x2,y2,x+0.5,y+0.5) / area;
            double w1 = edge_sign(x2,y2,x0,y0,x+0.5,y+0.5) / area;
            double w2 = 1.0 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            uint32_t c = 0xFFFFFFFF;
            if (r.frag_shader) {
                FragInput in;
                in.world = Vec3(0,0,0); in.normal = Vec3(0,0,1);
                in.uv = Vec2(0,0); in.color = Vec4(1,1,1,1); in.depth = 0.5;
                r.frag_shader(in, r.frag_user, c);
            }
            r.target.data[y * r.w + x] = c;
        }
}

// ============================================================================
// self test
// ============================================================================
// 简单片元：固定颜色
static void flat_frag(const FragInput&, void* user, uint32_t& out) {
    out = *(uint32_t*)user;
}

int raster_self_test() {
    int fail = 0;

    // 分配一个 32x32 小 buffer
    const int W = 32, H = 32;
    uint32_t* pix = new uint32_t[(size_t)W * H];
    nefu::gfxlib::Buffer buf = { pix, W, H };
    Rasterizer r;
    r.attach(buf);
    uint32_t white = 0xFFFFFFFF;
    r.frag_shader = flat_frag;
    r.frag_user = &white;
    r.backface_cull = false;

    // 清成黑色
    r.clear(0xFF000000);
    for (int i = 0; i < W * H; i++)
        if (pix[i] != 0xFF000000) { fail++; break; }

    // 画一个覆盖中心的三角形：NCC 坐标直接给 clip
    // 三角形顶点 clip = (x,y,z,w)，w=1 时 NDC=(x,y)
    Rasterizer::ClipVert v0, v1, v2;
    v0.clip = Vec4(-0.5, -0.5, 0, 1);
    v1.clip = Vec4( 0.5, -0.5, 0, 1);
    v2.clip = Vec4( 0.0,  0.5, 0, 1);
    v0.world = v1.world = v2.world = Vec3(0, 0, 0);
    v0.normal = v1.normal = v2.normal = Vec3(0, 0, 1);
    v0.uv = v1.uv = v2.uv = Vec2(0, 0);
    v0.color = v1.color = v2.color = Vec4(1, 1, 1, 1);
    r.draw_triangle(v0, v1, v2);

    // 中心像素 (16,16) 应被白色覆盖
    uint32_t center = pix[16 * W + 16];
    if ((center & 0xFFFFFF) != 0xFFFFFF) fail++;

    // 背景角落应仍是黑色
    if ((pix[0] & 0xFFFFFF) == 0xFFFFFF) fail++;

    r.detach();
    delete[] pix;

    // Liang-Barsky：完全在窗外的线段应被剔除
    {
        int x0 = -100, y0 = -100, x1 = -50, y1 = -50;
        if (clip_line_liang_barsky(x0, y0, x1, y1, 0, 0, 100, 100)) fail++;
    }
    // 跨窗口线段应被裁剪
    {
        int x0 = -10, y0 = 50, x1 = 110, y1 = 50;
        if (!clip_line_liang_barsky(x0, y0, x1, y1, 0, 0, 100, 100)) fail++;
        if (x0 != 0 || x1 != 100) fail++;
    }

    // alpha 混合
    {
        uint32_t r = alpha_blend(pack_color(1, 0, 0, 0.5), pack_color(0, 0, 1, 1));
        double rr, gg, bb;
        unpack_color(r, rr, gg, bb);
        // 0.5*红 + 0.5*蓝
        if (f_abs(rr - 0.5) > 0.02 || f_abs(bb - 0.5) > 0.02) fail++;
    }

    // mipmap：4x4 -> 2x2 -> 1x1
    {
        uint32_t src[16];
        for (int i = 0; i < 16; i++) src[i] = 0xFFFFFFFF;
        uint32_t* levels[4];
        int lw[4], lh[4];
        mipmap_generate(src, 4, 4, levels, lw, lh, 4);
        if (lw[0] != 4 || lh[0] != 4) fail++;
        if (lw[1] != 2 || lh[1] != 2) fail++;
        if (lw[2] != 1 || lh[2] != 1) fail++;
        for (int i = 0; i < 3; i++) delete[] levels[i];
    }

    // 扫描线：画到小 buffer
    {
        const int W = 16, H = 16;
        uint32_t* pix = new uint32_t[W*H];
        nefu::gfxlib::Buffer buf = { pix, W, H };
        Rasterizer r;
        r.attach(buf);
        r.clear(0xFF000000);
        uint32_t col = 0xFFFFFFFF;
        r.frag_shader = [](const FragInput&, void* u, uint32_t& out) {
            out = *(uint32_t*)u;
        };
        r.frag_user = &col;
        raster_scanline(r, 2, 2, 13, 2, 7, 13);
        if ((pix[8*W+7] & 0xFFFFFF) != 0xFFFFFF) fail++;
        r.detach();
        delete[] pix;
    }

    return fail;
}

} // namespace gfx3d
} // namespace nefu
