// nefuOS 数据序列化与编解码库 —— 图像格式模块
// imagec.h: BMP / TGA / PPM / PGM / PBM / QOI / PCX / ICO（无压缩或简单压缩）
//
// 内存中的统一图像表示为 RGBA8888（每像素 4 字节，行主序，从上到下）：
//   ImageBuf { w, h, channels, px }
// 各格式编解码都在这层表示上往返；只支持无压缩或 RLE/QOI 这类简单格式，
// JPEG/PNG 交给 third_party，不在本模块范围。
#pragma once
#include "../klib/klib.h"

namespace nefu {
namespace serialize {

// 统一内存图像：RGBA，px[y*w*4 + x*4 + 0..3] = R,G,B,A
struct ImageBuf {
    int w;
    int h;
    int channels;   // 3 或 4
    uint8_t* px;    // 像素缓冲（拥有）
    bool own;

    ImageBuf() : w(0), h(0), channels(4), px(0), own(false) {}
    void alloc(int width, int height, int ch = 4);
    void free_buf();
    // 取像素 (x,y) 的 R/G/B/A；越界返回 0
    uint8_t get_r(int x, int y) const;
    uint8_t get_g(int x, int y) const;
    uint8_t get_b(int x, int y) const;
    uint8_t get_a(int x, int y) const;
    void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
};

// ============================================================================
// BMP（BITMAPFILEHEADER 14 + BITMAPINFOHEADER 40，24/32 位，底部朝上 BGR）
// ============================================================================
bool bmp_read(const uint8_t* data, int len, ImageBuf& out);
int  bmp_write(const ImageBuf& img, uint8_t* out, int out_cap);  // 返回写出字节数，<0 失败
int  bmp_encoded_size(const ImageBuf& img);
int  bmp_self_test();

// ============================================================================
// TGA（type 2 未压缩真彩；读支持 type 10 RLE；8/24/32 位）
// ============================================================================
bool tga_read(const uint8_t* data, int len, ImageBuf& out);
int  tga_write(const ImageBuf& img, uint8_t* out, int out_cap);
int  tga_encoded_size(const ImageBuf& img);
int  tga_self_test();

// ============================================================================
// PPM / PGM / PBM（PNM 系列）
//   P6=二进制 RGB, P5=二进制灰度, P4=二进制位图; P3/P2/P1=ASCII 变体。
//   这里读写 P6/P5（最常用），并能自动识别读取 P3/P2/P1/P4。
// ============================================================================
bool ppm_read(const uint8_t* data, int len, ImageBuf& out);     // 自动识别 P1..P6
int  ppm_write(const ImageBuf& img, uint8_t* out, int out_cap, bool ascii = false);
int  ppm_encoded_size(const ImageBuf& img, bool ascii = false);
int  ppm_self_test();

// PGM（单通道灰度）单独接口
bool pgm_read(const uint8_t* data, int len, ImageBuf& out);
int  pgm_write(const ImageBuf& img, uint8_t* out, int out_cap);

// ============================================================================
// QOI（Quite OK Image，https://qoiformat.org）
//   头14字节("qoif"+w4+h4+ch1+cs1) + RGBA 块编码 + 尾8字节(00..01)。
// ============================================================================
bool qoi_read(const uint8_t* data, int len, ImageBuf& out);
int  qoi_write(const ImageBuf& img, uint8_t* out, int out_cap);
int  qoi_encoded_size(const ImageBuf& img);   // 上界估计
int  qoi_self_test();

// ============================================================================
// PCX（读取，1 字节 RLE，256 色调色板）
// ============================================================================
bool pcx_read(const uint8_t* data, int len, ImageBuf& out);
int  pcx_self_test();

// ============================================================================
// ICO（读取；解析目录，提取 BMP 子图）
// ============================================================================
struct IcoEntry {
    int w, h;
    int bpp;
};
bool ico_read(const uint8_t* data, int len, ImageBuf& out);
int   ico_count(const uint8_t* data, int len, IcoEntry* entries, int max);
int   ico_self_test();

int imagec_self_test();
int tga_rle_synthetic_test();
int qoi_run_boundary_test();
int pbm_synthetic_test();

} // namespace serialize
} // namespace nefu

// imagec.h 汇总：
//   - BMP (24/32 位) 读写
//   - TGA 读写（含 RLE）
//   - PPM/PGM/PBM 读写（ASCII/二进制）
//   - QOI 读写（run/index/diff/luma/rgb/rgba 块）
//   - PCX / ICO 读取（只读）
// 只做无压缩或简单压缩格式，JPEG/PNG 走 third_party。