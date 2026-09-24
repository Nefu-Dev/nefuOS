// nefuOS stb_image + nanosvg (SVG) integration — declarations.
// Implementation lives in stb_image_wrap.cpp + nanosvg_impl.c (C unit).
#pragma once
#include <stdint.h>
namespace nefu {
bool stbi_decode_mem(const uint8_t* data, int len, int* w, int* h, uint8_t** out);
bool stbi_info_mem(const uint8_t* data, int len, int* w, int* h);
enum ImgFmt { IMG_UNKNOWN = 0, IMG_PNG = 1, IMG_JPEG = 2, IMG_BMP = 3,
              IMG_GIF = 4, IMG_TGA = 5, IMG_SVG = 6, IMG_WEBP = 7 };
ImgFmt stbi_sniff(const uint8_t* d, int len);
bool nsvg_decode_mem(const uint8_t* data, int len, int max_px, int* w, int* h, uint8_t** out);
}
extern "C" {
int nefu_webp_decode(const unsigned char* data, unsigned len, int* w, int* h, unsigned char** rgba);
}
