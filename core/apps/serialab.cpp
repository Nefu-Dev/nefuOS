// nefuOS 格式转换台（Serialab）—— 数据序列化库的可视化前端
//
// 一个窗口应用：选择源格式 / 目标格式，对内置样例做往返转换，并在窗口里预览结果。
// 支持的转换族：
//   CSV -> JSON, INI -> JSON, XML 美化, BMP -> PPM, BMP -> QOI, WAV 信息查看。
//
// 按键：
//   [S] 切换源格式   [T] 切换目标格式   [Enter/空格] 执行转换
//   [Esc] 关闭
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../serialize/serialize_all.h"
#include <math.h>

namespace nefu {

namespace {

const int SA_W = 620, SA_H = 440;

// 源/目标格式枚举（本应用内部）
enum {
    FMT_CSV = 0,
    FMT_INI,
    FMT_XML,
    FMT_TOML,
    FMT_BMP,
    FMT_PPM,
    FMT_QOI,
    FMT_WAV,
    FMT_COUNT
};
const char* FMT_NAMES[FMT_COUNT] = {
    "CSV", "INI", "XML", "TOML", "BMP", "PPM", "QOI", "WAV"
};

// 内置样例
const char* SAMPLE_CSV =
    "name,age,city\n"
    "\"Alice \"\"A\"\"\",30,Qingdao\n"
    "Bob,25,Beijing\n"
    "Carol,33,\"Shanghai\nPudong\"\n";

const char* SAMPLE_INI =
    "; 主配置\n"
    "[display]\n"
    "width = 800\n"
    "height = 600\n"
    "theme = dark\n"
    "[network]\n"
    "port = 8080\n"
    "debug = true\n";

const char* SAMPLE_XML =
    "<?xml version=\"1.0\"?>\n"
    "<root>\n"
    "  <item id=\"1\">Apple</item>\n"
    "  <item id=\"2\">Banana</item>\n"
    "</root>\n";

const char* SAMPLE_TOML =
    "[server]\n"
    "host = \"0.0.0.0\"\n"
    "port = 8080\n"
    "enabled = true\n";

struct Serialab {
    int src_fmt;
    int dst_fmt;
    char preview[640];
    bool has_result;
    char status[128];

    Serialab() : src_fmt(FMT_CSV), dst_fmt(FMT_INI), has_result(false) {
        preview[0] = 0;
        ksprintf(status, sizeof(status), "Press Enter to convert");
    }

    const char* sample_text() const {
        switch (src_fmt) {
        case FMT_CSV: return SAMPLE_CSV;
        case FMT_INI: return SAMPLE_INI;
        case FMT_XML: return SAMPLE_XML;
        case FMT_TOML: return SAMPLE_TOML;
        default: return SAMPLE_CSV;
        }
    }

    void convert();
    void paint(Surface& s);
};

// ---------------------------------------------------------------------------
// 转换实现：真实调用 serialize 库的 parse/write
// ---------------------------------------------------------------------------
void Serialab::convert() {
    has_result = false;
    preview[0] = 0;
    char* p = preview;
    int left = sizeof(preview);

    // 图片族：造一张 4x3 渐变图，BMP 编码后转 PPM/QOI
    if (src_fmt == FMT_BMP && (dst_fmt == FMT_PPM || dst_fmt == FMT_QOI)) {
        ImageBuf img;
        img.alloc(4, 3, 4);
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 4; x++)
                img.set_pixel(x, y, (uint8_t)(x * 60), (uint8_t)(y * 80), 200, 255);
        int bmp_sz = bmp_encoded_size(img);
        uint8_t* bmp = new uint8_t[bmp_sz];
        bmp_write(img, bmp, bmp_sz);
        ImageBuf back;
        bool rok = bmp_read(bmp, bmp_sz, back);
        int n = ksprintf(p, left, "BMP read: %s  %dx%d\n", rok ? "OK" : "FAIL", back.w, back.h);
        p += n; left -= n;
        n = ksprintf(p, left, "px(0,0)=#%02X%02X%02X px(3,2)=#%02X%02X%02X\n",
                     back.get_r(0, 0), back.get_g(0, 0), back.get_b(0, 0),
                     back.get_r(3, 2), back.get_g(3, 2), back.get_b(3, 2));
        p += n; left -= n;
        if (dst_fmt == FMT_QOI) {
            int qsz = qoi_encoded_size(back);
            uint8_t* qoi = new uint8_t[qsz];
            int qn = qoi_write(back, qoi, qsz);
            n = ksprintf(p, left, "QOI: %d bytes (raw %d)\n",
                         qn, back.w * back.h * 4);
            delete[] qoi;
        } else {
            uint8_t ppm[128];
            int pn = ppm_write(back, ppm, sizeof(ppm), false);
            n = ksprintf(p, left, "PPM header: %.48s\n", (const char*)ppm);
        }
        back.free_buf();
        delete[] bmp;
        has_result = true;
        ksprintf(status, sizeof(status), "Converted BMP -> %s OK", FMT_NAMES[dst_fmt]);
        return;
    }

    // WAV 信息查看：合成 0.1 秒 440Hz，写出后读回头信息
    if (src_fmt == FMT_WAV) {
        AudioBuf a;
        int frames = 4410;
        a.alloc(44100, 1, frames);
        for (int i = 0; i < frames; i++)
            a.set(i, 0, 0.5f * sinf(2.0f * 3.14159265f * 440.0f * i / 44100.0f));
        int sz = wav_encoded_size(a, 16);
        uint8_t* buf = new uint8_t[sz];
        wav_write(a, buf, sz, 16);
        WavInfo info;
        if (wav_read_info(buf, sz, info)) {
            int n = ksprintf(p, left, "WAV info:\n");
            p += n; left -= n;
            n = ksprintf(p, left, "  channels=%d  sample_rate=%d Hz\n", info.channels, info.sample_rate);
            p += n; left -= n;
            n = ksprintf(p, left, "  bits=%d  data_bytes=%d\n", info.bits, info.data_bytes);
            p += n; left -= n;
            int ms = info.data_bytes / (info.sample_rate * info.channels * info.bits / 8 / 1000 + 1);
            n = ksprintf(p, left, "  duration=%d ms\n", ms);
            p += n; left -= n;
        }
        delete[] buf;
        has_result = true;
        ksprintf(status, sizeof(status), "WAV info shown");
        return;
    }

    // 文本族
    const char* src = sample_text();
    int n = ksprintf(p, left, "Parsed %s:\n", FMT_NAMES[src_fmt]);
    p += n; left -= n;

    if (src_fmt == FMT_CSV) {
        CsvDoc doc;
        if (!csv_parse(src, doc)) { ksprintf(status, sizeof(status), "CSV parse fail"); return; }
        n = ksprintf(p, left, "  rows=%d cols=%d\n", doc.rows_count(), doc.cols_count());
        p += n; left -= n;
        for (int r = 0; r < doc.rows_count() && r < 4; r++) {
            n = ksprintf(p, left, "  row%d: %s | %s | %s\n", r,
                         doc.cell(r, 0), doc.cell(r, 1), doc.cell(r, 2));
            p += n; left -= n;
        }
    } else if (src_fmt == FMT_INI) {
        IniDoc doc;
        if (!ini_parse(src, doc)) { ksprintf(status, sizeof(status), "INI parse fail"); return; }
        n = ksprintf(p, left, "  sections=%d\n", doc.sections.size());
        p += n; left -= n;
        n = ksprintf(p, left, "  display.width=%s network.port=%s\n",
                     doc.get("display", "width"), doc.get("network", "port"));
        p += n; left -= n;
    } else if (src_fmt == FMT_XML) {
        XmlDoc doc;
        if (!xml_parse(src, doc)) { ksprintf(status, sizeof(status), "XML parse fail"); return; }
        int kids = doc.root ? doc.root->children.size() : 0;
        n = ksprintf(p, left, "  root=<%s> children=%d\n",
                     doc.root ? doc.root->name.c_str() : "?", kids);
        p += n; left -= n;
        // 美化回写
        String pretty;
        xml_write(doc, pretty, true);
        n = ksprintf(p, left, "  pretty: %.200s\n", pretty.c_str());
        p += n; left -= n;
        xml_free(doc);
    } else if (src_fmt == FMT_TOML) {
        TomlDoc doc;
        if (!toml_parse(src, doc)) { ksprintf(status, sizeof(status), "TOML parse fail"); return; }
        n = ksprintf(p, left, "  root members=%d\n", doc.root ? doc.root->obj.size() : 0);
        p += n; left -= n;
        if (doc.root) free_value(doc.root);
    }

    has_result = true;
    ksprintf(status, sizeof(status), "Parsed %s OK", FMT_NAMES[src_fmt]);
}

void Serialab::paint(Surface& s) {
    s.fill(0x00FAF8EF);
    gfx::text_scale(s, 10, 8, "Serialab - Format Lab", 0x00776756, 0x00FAF8EF, 2);
    char buf[128];
    ksprintf(buf, sizeof(buf), "Source: %s   Target: %s   [S/T] change  [Enter] convert",
             FMT_NAMES[src_fmt], FMT_NAMES[dst_fmt]);
    gfx::text(s, 10, 40, buf, 0x00505050, 0x00FAF8EF);
    int box_y = 64;
    gfx::fillrect(s, 8, box_y, SA_W - 16, SA_H - 110, 0x00FFFFFF);
    gfx::fillrect(s, 8, box_y, SA_W - 16, 20, 0x00DDD8D0);
    gfx::text(s, 14, box_y + 4, "Preview", 0x00606060, 0x00DDD8D0);
    if (has_result) {
        int ly = box_y + 26;
        const char* c = preview;
        while (*c && ly < SA_H - 60) {
            char line[120]; int k = 0;
            while (*c && *c != '\n' && k < (int)sizeof(line) - 1) line[k++] = *c++;
            line[k] = 0;
            if (*c == '\n') c++;
            gfx::text(s, 16, ly, line, 0x00202020, 0x00FFFFFF);
            ly += 14;
        }
    } else {
        gfx::text(s, 16, box_y + 30, "(no result - press Enter)", 0x00909090, 0x00FFFFFF);
    }
    gfx::text(s, 10, SA_H - 22, status, 0x0027AE60, 0x00FAF8EF);
    gfx::text(s, 10, SA_H - 8, "S: source  T: target  Enter: convert  Esc: close",
              0x00909090, 0x00FAF8EF);
}

} // namespace

static Serialab* sa_of(Window* w) { return (Serialab*)w->userdata; }
static void sa_paint(Window* w) { sa_of(w)->paint(w->back); }
static void sa_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    Serialab* sa = sa_of(w);
    if (e->ascii == 's' || e->ascii == 'S') { sa->src_fmt = (sa->src_fmt + 1) % FMT_COUNT; return; }
    if (e->ascii == 't' || e->ascii == 'T') { sa->dst_fmt = (sa->dst_fmt + 1) % FMT_COUNT; return; }
    if (e->keycode == KEY_ENTER || e->keycode == KEY_SPACE) { sa->convert(); return; }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
}
static void sa_close(Window* w) {
    if (w->userdata) delete (Serialab*)w->userdata;
    w->userdata = 0;
}

void serialab_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Serialab", x, y, SA_W, SA_H);
    if (!w) return;
    Serialab* sa = new Serialab();
    w->userdata = sa;
    w->on_paint = sa_paint;
    w->on_key = sa_key;
    w->on_close = sa_close;
    g_wm->raise(w);
}

} // namespace nefu
// 接线：在 apps.h 注册 APP_SERIALAB 与 serialab_launch()，在 desktop/store 加图标。
// 终端命令见 term_ext.cpp。