// nefuOS 密码学实验室 —— 交互式加解密/哈希小工具
// 窗口应用：参考 algoviz.cpp 的 create_window + userdata + 回调模式。
//
// 操作（键盘驱动，OS 内无鼠标文本框）：
//   字母/数字      —— 追加到"待处理文本"或"密钥"（按当前焦点区）
//   Tab           —— 在 [文本区] 与 [密钥区] 之间切换焦点
//   Backspace     —— 删除焦点区最后一个字符
//   1..9          —— 选择算法：
//        1 XOR   2 RC4    3 AES-ECB   4 ChaCha20
//        5 SHA-256(内置)  6 SHA3-256  7 Base64   8 Base58   9 ROT13
//   E             —— 执行加密/编码
//   D             —— 执行解密/解码
//   H             —— 计算哈希（仅对哈希类算法有意义）
//   C             —— 清空输入
//   Esc           —— 关闭窗口
//
// 结果以十六进制或可见字符串显示在结果区。
// 本文件仅做 UI 胶水，真正的算法在 core/crypto/ 各模块中。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../crypto/crypto_all.h"
#include "../lib/hash.h"

namespace nefu {
namespace {

const int LAB_W = 620, LAB_H = 440;

// 算法索引
enum {
    ALG_XOR = 0,
    ALG_RC4,
    ALG_AES,
    ALG_CHACHA,
    ALG_SHA256,
    ALG_SHA3,
    ALG_B64,
    ALG_B58,
    ALG_ROT13,
    ALG_NUM
};

const char* ALG_NAMES[ALG_NUM] = {
    "XOR", "RC4", "AES-ECB", "ChaCha20",
    "SHA-256", "SHA3-256", "Base64", "Base58", "ROT13"
};

// 焦点区
enum { FOCUS_TEXT = 0, FOCUS_KEY = 1 };

struct CryptoLab {
    char text[256];      // 待处理文本
    int  textlen;
    char key[128];       // 密钥
    int  keylen;
    int  alg;            // 当前算法
    int  focus;          // 当前焦点
    char result[512];    // 结果（可见字符串或 hex）
    int  resultlen;
    char status[64];     // 状态提示

    CryptoLab() {
        textlen = 0; text[0] = 0;
        keylen = 0;  key[0] = 0;
        alg = 0; focus = FOCUS_TEXT;
        resultlen = 0; result[0] = 0;
        ksprintf(status, sizeof(status), "Ready. Tab=焦点, E=加密, D=解密, H=哈希");
    }

    // 向焦点区追加一个可见字符
    void append(char c) {
        if (focus == FOCUS_TEXT) {
            if (textlen < (int)sizeof(text) - 1) { text[textlen++] = c; text[textlen] = 0; }
        } else {
            if (keylen < (int)sizeof(key) - 1) { key[keylen++] = c; key[keylen] = 0; }
        }
    }
    void backspace() {
        if (focus == FOCUS_TEXT && textlen > 0) { text[--textlen] = 0; }
        else if (focus == FOCUS_KEY && keylen > 0) { key[--keylen] = 0; }
    }

    // 把二进制转 hex 写入 result
    void to_hex_result(const uint8_t* p, int n) {
        static const char* h = "0123456789abcdef";
        int o = 0;
        for (int i = 0; i < n && o < (int)sizeof(result) - 2; i++) {
            result[o++] = h[p[i] >> 4];
            result[o++] = h[p[i] & 15];
        }
        result[o] = 0;
        resultlen = o;
    }

    // 加密/编码
    void do_encrypt() {
        result[0] = 0; resultlen = 0;
        const uint8_t* pt = (const uint8_t*)text;
        int n = textlen;
        switch (alg) {
        case ALG_XOR: {
            uint8_t out[256];
            crypto::xor_crypt((const uint8_t*)key, keylen ? keylen : 1, pt, out, n);
            to_hex_result(out, n);
            ksprintf(status, sizeof(status), "XOR 加密 -> hex");
            break;
        }
        case ALG_RC4: {
            crypto::RC4 r; r.init((const uint8_t*)key, keylen ? keylen : 1);
            uint8_t out[256]; r.crypt(pt, out, n);
            to_hex_result(out, n);
            ksprintf(status, sizeof(status), "RC4 加密 -> hex");
            break;
        }
        case ALG_AES: {
            // AES-128 ECB：按 16 字节分组（不足补 0）
            uint8_t k[16]; for (int i = 0; i < 16; i++) k[i] = (uint8_t)(i < keylen ? key[i] : 0);
            crypto::AES a; a.init(k, 16);
            int blocks = (n + 15) / 16; if (blocks < 1) blocks = 1;
            uint8_t out[256];
            for (int i = 0; i < blocks; i++) {
                uint8_t inb[16] = {0};
                for (int b = 0; b < 16 && i*16+b < n; b++) inb[b] = pt[i*16+b];
                a.encrypt_block(inb, out + i*16);
            }
            to_hex_result(out, blocks * 16);
            ksprintf(status, sizeof(status), "AES-128-ECB 加密 -> hex");
            break;
        }
        case ALG_CHACHA: {
            uint8_t k[32]; for (int i = 0; i < 32; i++) k[i] = (uint8_t)(i < keylen ? key[i] : 0);
            uint8_t nonce[12] = {0};
            uint8_t out[256];
            crypto::chacha20_crypt(k, nonce, 1, pt, out, n);
            to_hex_result(out, n);
            ksprintf(status, sizeof(status), "ChaCha20 加密 -> hex");
            break;
        }
        case ALG_B64: {
            // 复用 core::lib::hash.h 的 Base64
            hash::base64_encode(pt, n, result, sizeof(result));
            ksprintf(status, sizeof(status), "Base64 编码");
            break;
        }
        case ALG_B58: {
            crypto::base58_encode(pt, n, result, sizeof(result));
            ksprintf(status, sizeof(status), "Base58 编码");
            break;
        }
        case ALG_ROT13: {
            crypto::rot13(text, result);
            ksprintf(status, sizeof(status), "ROT13");
            break;
        }
        default:
            ksprintf(status, sizeof(status), "该算法请用 H(哈希)");
            break;
        }
    }

    // 解密/解码（演示性：XOR/RC4/AES/ChaCha/ROT13 对称；base 类反向）
    void do_decrypt() {
        result[0] = 0; resultlen = 0;
        switch (alg) {
        case ALG_XOR: {
            // 输入视为 hex，解 hex 后异或
            uint8_t buf[256]; int n = crypto::hex_decode(text, buf, sizeof(buf));
            uint8_t out[256];
            crypto::xor_crypt((const uint8_t*)key, keylen ? keylen : 1, buf, out, n);
            for (int i = 0; i < n; i++) result[i] = (char)out[i];
            result[n] = 0;
            ksprintf(status, sizeof(status), "XOR 解密");
            break;
        }
        case ALG_ROT13:
            crypto::rot13(text, result);
            ksprintf(status, sizeof(status), "ROT13");
            break;
        case ALG_B64: {
            uint8_t out[256]; int n = hash::base64_decode(text, out, sizeof(out));
            for (int i = 0; i < n; i++) result[i] = (char)out[i]; result[n] = 0;
            ksprintf(status, sizeof(status), "Base64 解码");
            break;
        }
        case ALG_B58: {
            uint8_t out[256]; int n = crypto::base58_decode(text, out, sizeof(out));
            for (int i = 0; i < n; i++) result[i] = (char)out[i]; result[n] = 0;
            ksprintf(status, sizeof(status), "Base58 解码");
            break;
        }
        default:
            ksprintf(status, sizeof(status), "对称算法：E 加密后复制 hex 到文本区再 D 解密");
            break;
        }
    }

    // 哈希
    void do_hash() {
        result[0] = 0; resultlen = 0;
        uint8_t d[64];
        switch (alg) {
        case ALG_SHA256: {
            crypto::sha512(text, textlen, d);
            to_hex_result(d, 64);
            ksprintf(status, sizeof(status), "SHA-512");
            break;
        }
        case ALG_SHA3:
            crypto::sha3_256(text, textlen, d);
            to_hex_result(d, 32);
            ksprintf(status, sizeof(status), "SHA3-256");
            break;
        default:
            ksprintf(status, sizeof(status), "选 5(SHA-256) 或 6(SHA3) 后按 H");
            break;
        }
    }

    void paint(Surface& s) {
        s.fill(0x00FAF8EF);
        gfx::text_scale(s, 12, 8, "Crypto Lab", 0x002A9D8F, 0x00FAF8EF, 2);
        char buf[128];
        ksprintf(buf, sizeof(buf), "算法: %s  (1-9 切换)", ALG_NAMES[alg]);
        gfx::text(s, 12, 36, buf, 0x00264653, 0x00FAF8EF);
        // 焦点高亮
        gfx::text(s, 12, 58, focus == FOCUS_TEXT ? "> 文本:" : "  文本:", 0x00E76F51, 0x00FAF8EF);
        gfx::text(s, 90, 58, text, 0x00333333, 0x00FAF8EF);
        gfx::fillrect(s, 12, 74, LAB_W - 24, 1, 0x00CCC0B3);
        gfx::text(s, 12, 82, focus == FOCUS_KEY ? "> 密钥:" : "  密钥:", 0x00E76F51, 0x00FAF8EF);
        gfx::text(s, 90, 82, key, 0x00333333, 0x00FAF8EF);
        gfx::fillrect(s, 12, 98, LAB_W - 24, 1, 0x00CCC0B3);
        gfx::text(s, 12, 108, "结果:", 0x00264653, 0x00FAF8EF);
        // 结果可能很长，简单折行
        gfx::text(s, 12, 128, result, 0x001D3557, 0x00FAF8EF);
        gfx::fillrect(s, 12, 160, LAB_W - 24, 1, 0x00CCC0B3);
        gfx::text(s, 12, 170, status, 0x00666666, 0x00FAF8EF);
        gfx::text(s, 12, LAB_H - 40,
                  "Tab:焦点  E:加密  D:解密  H:哈希  C:清空", 0x00909090, 0x00FAF8EF);
        gfx::text(s, 12, LAB_H - 24, "1 XOR 2 RC4 3 AES 4 Cha 5 SHA256 6 SHA3 7 B64 8 B58 9 ROT13   Esc:close",
                  0x00909090, 0x00FAF8EF);
    }
};

} // namespace

static CryptoLab* lab_of(Window* w) { return (CryptoLab*)w->userdata; }

static void lab_paint(Window* w) { lab_of(w)->paint(w->back); }

static void lab_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    CryptoLab* L = lab_of(w);
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }
    if (e->keycode == KEY_TAB) { L->focus = (L->focus == FOCUS_TEXT) ? FOCUS_KEY : FOCUS_TEXT; return; }
    if (e->keycode == KEY_BACKSPACE) { L->backspace(); return; }
    if (e->ascii == 'c' || e->ascii == 'C') {
        L->text[0]=0; L->textlen=0; L->key[0]=0; L->keylen=0;
        L->result[0]=0; L->resultlen=0; return;
    }
    if (e->ascii == 'e' || e->ascii == 'E') { L->do_encrypt(); return; }
    if (e->ascii == 'd' || e->ascii == 'D') { L->do_decrypt(); return; }
    if (e->ascii == 'h' || e->ascii == 'H') { L->do_hash(); return; }
    if (e->ascii >= '1' && e->ascii <= '9') {
        int a = e->ascii - '1';
        if (a < ALG_NUM) L->alg = a;
        return;
    }
    // 可见字符输入
    if (e->ascii >= 32 && e->ascii < 127) L->append((char)e->ascii);
}

static void lab_close(Window* w) {
    if (w->userdata) delete (CryptoLab*)w->userdata;
    w->userdata = 0;
}

void cryptolab_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Crypto Lab", x, y, LAB_W, LAB_H);
    if (!w) return;
    CryptoLab* L = new CryptoLab();
    w->userdata = L;
    w->on_paint = lab_paint;
    w->on_key = lab_key;
    w->on_close = lab_close;
    g_wm->raise(w);
}

} // namespace nefu
