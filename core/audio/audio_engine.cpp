// nefuOS audio engine - integer facade over the real sound-card output.
// Compiled with the normal (FPU) ABI, so it may use the float mixer freely;
// every function it exports is integer-only.
#include "audio_engine.h"
#include "audio_manager.h"
#include "../drivers/audio.h"
#include "../klib/klib.h"
#include "../platform.h"
#include "../vfs/vfs.h"
#include <string.h>

namespace nefu {
namespace audio {

static bool s_engine_init = false;

// ---- media session state ----
static bool     s_media_open     = false;
static int      s_media_stream   = -1;
static uint32_t s_media_rate     = 44100;
static uint32_t s_media_frames   = 0;    // total PCM frames
static uint8_t* s_media_owned    = 0;    // owned copy of the WAV bytes
static uint32_t s_media_owned_len= 0;

bool engine_init(void) {
    if (s_engine_init) return g_audio.hw_ok_state();
    g_audio.init();
    s_engine_init = true;
    if (g_audio.hardware_active())
        klogf("audio: engine ready on %s\n", g_audio.hardware_name());
    return g_audio.hw_ok_state();
}

void engine_pump(void) {
    if (s_engine_init) g_audio.hw_pump();
}

bool engine_available(void) { return g_audio.hw_ok_state(); }

const char* engine_device_name(void) { return g_audio.hw_name_state(); }

// ---- WAV header parser (16-bit PCM only) ----
static bool wav_info(const uint8_t* d, uint32_t size,
                     uint32_t& rate, uint32_t& channels, uint32_t& bits,
                     uint32_t& data_off, uint32_t& data_len) {
    if (!d || size < 44) return false;
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return false;
    uint32_t off = 12;
    bool got_fmt = false, got_data = false;
    while (off + 8 <= size) {
        uint32_t id = 0, len = 0;
        memcpy(&id, d + off, 4);
        memcpy(&len, d + off + 4, 4);
        if (off + 8 + len > size) return false;
        if (id == 0x20746D66u) {                      // "fmt "
            if (len < 16) return false;
            uint16_t fmt = (uint16_t)((uint16_t)d[off + 8] | ((uint16_t)d[off + 9] << 8));
            if (fmt != 1) return false;               // PCM only
            channels = (uint32_t)((uint16_t)d[off + 10] | ((uint16_t)d[off + 11] << 8));
            memcpy(&rate, d + off + 12, 4);
            bits = (uint32_t)((uint16_t)d[off + 22] | ((uint16_t)d[off + 23] << 8));
            got_fmt = true;
        } else if (id == 0x61746164u) {               // "data"
            data_off = off + 8;
            data_len = len;
            got_data = true;
        }
        off += 8 + len + (len & 1u);
    }
    return got_fmt && got_data;
}

// ---- one-shot blocking playback ----
bool engine_play_wav_mem(const uint8_t* data, uint32_t size) {
    if (!data || size < 44) return false;
    if (!engine_init()) return false;
    uint32_t rate = 0, ch = 0, bits = 0, doff = 0, dlen = 0;
    if (!wav_info(data, size, rate, ch, bits, doff, dlen)) return false;
    if (rate == 0 || ch == 0 || bits != 16) return false;
    int id = g_audio.create_stream(AUDIO_FORMAT_PCM_S16_LE, rate,
                                   (uint8_t)(ch > 2 ? 2 : ch), 4096);
    if (id < 0) return false;
    g_audio.load_stream_data(id, data + doff, dlen);
    g_audio.play_stream(id);
    uint32_t total = dlen / (ch * 2u);
    uint32_t guard = 0;
    while (g_audio.get_stream_position(id) < total && engine_available()) {
        engine_pump();
        for (volatile uint32_t spin = 0; spin < 4000; spin++) {}
        if (++guard > 4000000u) break;      // safety: never hang
    }
    g_audio.stop_stream(id);
    g_audio.destroy_stream(id);
    return true;
}

// ---- media session ----
bool media_open_mem(const uint8_t* data, uint32_t size) {
    media_close();
    if (!data || size < 44) return false;
    if (!engine_init()) return false;
    uint32_t rate = 0, ch = 0, bits = 0, doff = 0, dlen = 0;
    if (!wav_info(data, size, rate, ch, bits, doff, dlen)) return false;
    if (rate == 0 || ch == 0 || bits != 16) return false;
    s_media_owned = (uint8_t*)kalloc(size);
    if (!s_media_owned) return false;
    memcpy(s_media_owned, data, size);
    s_media_owned_len = size;
    s_media_rate = rate;
    s_media_frames = dlen / (ch * 2u);
    s_media_stream = g_audio.create_stream(AUDIO_FORMAT_PCM_S16_LE, rate,
                                           (uint8_t)(ch > 2 ? 2 : ch), 4096);
    if (s_media_stream < 0) {
        kfree(s_media_owned);
        s_media_owned = 0;
        s_media_owned_len = 0;
        return false;
    }
    g_audio.load_stream_data(s_media_stream, s_media_owned + doff, dlen);
    g_audio.set_stream_position(s_media_stream, 0);
    s_media_open = true;
    klogf("audio: media open %u frames @ %u Hz (%u ch)\n", s_media_frames, rate, ch);
    return true;
}

bool media_open_path(const char* vfs_path) {
    if (!vfs_path || !vfs_path[0] || !g_vfs) return false;
    FSNode* f = g_vfs->resolve(vfs_path);
    if (!f || f->is_dir || !f->data || f->size < 44) return false;
    return media_open_mem(f->data, f->size);
}

void media_close(void) {
    if (s_media_stream >= 0) {
        g_audio.stop_stream(s_media_stream);
        g_audio.destroy_stream(s_media_stream);
        s_media_stream = -1;
    }
    if (s_media_owned) {
        kfree(s_media_owned);
        s_media_owned = 0;
    }
    s_media_owned_len = 0;
    s_media_open = false;
    s_media_frames = 0;
}

bool media_play(void) {
    if (!s_media_open || s_media_stream < 0) return false;
    g_audio.play_stream(s_media_stream);
    return true;
}

void media_pause(void) {
    if (s_media_open && s_media_stream >= 0) g_audio.pause_stream(s_media_stream);
}

void media_stop(void) {
    if (!s_media_open || s_media_stream < 0) return;
    g_audio.stop_stream(s_media_stream);
    g_audio.set_stream_position(s_media_stream, 0);
}

bool media_seek_sec(int sec) {
    if (!s_media_open || s_media_stream < 0 || s_media_rate == 0) return false;
    if (sec < 0) sec = 0;
    uint32_t pos = (uint32_t)sec * s_media_rate;
    if (pos > s_media_frames) pos = s_media_frames;
    g_audio.set_stream_position(s_media_stream, pos);
    return true;
}

int media_position_sec(void) {
    if (!s_media_open || s_media_stream < 0 || s_media_rate == 0) return 0;
    uint32_t pos = g_audio.get_stream_position(s_media_stream);
    if (pos >= s_media_frames) return (int)(s_media_frames / s_media_rate);
    return (int)(pos / s_media_rate);
}

int media_duration_sec(void) {
    if (!s_media_open || s_media_rate == 0) return -1;
    return (int)(s_media_frames / s_media_rate);
}

void media_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_audio.set_master_volume((float)percent / 100.0f);
}

bool media_has_video(void) { return false; }

bool media_get_info(uint32_t* rate, uint32_t* total_frames) {
    if (!s_media_open) return false;
    if (rate) *rate = s_media_rate;
    if (total_frames) *total_frames = s_media_frames;
    return true;
}

} // namespace audio
} // namespace nefu
