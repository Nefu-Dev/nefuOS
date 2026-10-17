// nefuOS audio driver - real mainstream sound cards.
//   * Intel AC'97 (82801AA/BA/CA/DB/EB/FB/GB + VIA) - NABMBAR DMA, BDL ring
//   * Intel HD Audio (ICH6/7/9/10, 5/6/7 series)    - CORB/RIRB + SD0 stream
// Block-based output: submit() hands a 16-bit stereo PCM block to the DMA
// engine, pump() (called from the periodic loop) detects completion and
// advances the play position.  Pure integer, freestanding.
#include "audio.h"
#include "pci.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace drivers {
namespace audio {

// ===================== shared state =====================
static int       s_backend      = AUDIO_NONE;
static uint32_t  s_rate         = 44100;
static uint32_t  s_channels     = 2;
static uint32_t  s_played_frames= 0;
static int       s_volume       = 70;

// ===================== AC'97 (Intel ICH / VIA) =====================
// NABMBAR (BAR0) register map
#define AC97_PCM_VOL     0x02u   // PCM Out volume (0 = max)
#define AC97_BDL_BASE    0x10u   // PCM Out buffer descriptor list base
#define AC97_POSITION    0x14u   // PCM Out position (read only)
#define AC97_BM_CTRL     0x16u   // PCM Out bus master control (bit0 rst, bit1 run)
#define AC97_LVI         0x18u   // PCM Out last valid index
#define AC97_BM_STATUS   0x1Au   // PCM Out status (bit0 = ring empty)
#define AC97_GLOB_CNT    0x2Cu   // global control (bit0 = cold reset)
#define AC97_GLOB_STA    0x2Eu   // global status
#define AC97_PCM_RATE    0x32u   // PCM Out sample rate

#define AC97_BLOCK_BYTES (AUDIO_OUTPUT_BLOCK_FRAMES * 4u)   // 8 KB

static volatile uint32_t* s_ac97      = 0;   // NABMBAR
static uint8_t* s_ac97_bdl            = 0;   // 128-byte aligned BDL
static uint8_t* s_ac97_buf            = 0;   // 8 KB DMA buffer
static bool     s_ac97_bdl_written    = false;
static bool     s_ac97_playing        = false;
static uint32_t s_ac97_block_bytes    = 0;

static inline uint32_t mmio_rd32(uint32_t reg) { return s_ac97[reg / 4u]; }
static inline void mmio_wr32(uint32_t reg, uint32_t v) { s_ac97[reg / 4u] = v; }

static bool ac97_probe_and_init(void) {
    // QEMU -soundhw ac97 exposes 8086:2415; real boards use the ICH family
    static const uint16_t ac97_ids[8] = {
        0x2415, 0x2445, 0x2485, 0x24C5, 0x24D5, 0x266E, 0x27DE, 0x3058  // 3058 = VIA
    };
    PciDev list[64];
    int n = pci_scan(list, 64);
    PciDev dev;
    dev.valid = false;
    for (int i = 0; i < n; i++) {
        if (list[i].vendor == 0x8086 || list[i].vendor == 0x1106) {
            for (int k = 0; k < 8; k++) {
                if (list[i].device == ac97_ids[k]) { dev = list[i]; break; }
            }
        }
        if (dev.valid) break;
    }
    klogf("audio: ac97 scan: %d devices, match=%d\n", n, dev.valid ? 1 : 0);
    if (!dev.valid) return false;

    bool is_io = false;
    uint64_t bar = pci_bar_address(dev, 0, &is_io);
    klogf("audio: ac97 bar0=0x%08X io=%d\n", (unsigned)bar, is_io ? 1 : 0);
    if (!bar || is_io) return false;
    pci_enable_busmaster(dev);
    s_ac97 = (volatile uint32_t*)(uintptr_t)bar;

    // cold reset: pulse GLOB_CNT bit0, wait for GLOB_STA bit0 (codec ready)
    mmio_wr32(AC97_GLOB_CNT, 0x0001u);
    for (uint32_t i = 0; i < 200000u; i++) pci_io_wait();
    mmio_wr32(AC97_GLOB_CNT, 0x0000u);
    for (uint32_t i = 0; i < 200000u; i++) pci_io_wait();
    uint32_t gsta = mmio_rd32(AC97_GLOB_STA);
    klogf("audio: ac97 glob_sta=0x%04X\n", gsta);
    if (!(gsta & 0x0001u)) { s_ac97 = 0; return false; }   // no codec attached

    // allocate BDL (128-byte aligned) + DMA buffer
    s_ac97_bdl = (uint8_t*)kalloc(128u + 16u);
    uintptr_t bdl = ((uintptr_t)s_ac97_bdl + 127u) & ~(uintptr_t)127u;
    s_ac97_buf = (uint8_t*)kalloc(AC97_BLOCK_BYTES + 64u);
    uintptr_t buf = ((uintptr_t)s_ac97_buf + 63u) & ~(uintptr_t)63u;
    s_ac97_bdl_written = false;

    // initial descriptor: filled on first submit
    uint32_t* bd = (uint32_t*)bdl;
    bd[0] = (uint32_t)buf;
    bd[1] = (uint32_t)((AC97_BLOCK_BYTES - 1) & 0x3FFFu);
    s_ac97_bdl = (uint8_t*)bdl;
    s_ac97_buf = (uint8_t*)buf;

    // master volume to maximum (0), unmuted
    mmio_wr32(AC97_PCM_VOL, 0x0000u);
    // sample rate 44100 (stereo 16-bit)
    mmio_wr32(AC97_PCM_RATE, 44100u);

    // stop the engine first (clean state)
    mmio_wr32(AC97_BM_CTRL, 0x0000u);
    s_ac97_playing = false;
    klogf("audio: AC'97 controller ready (NABMBAR 0x%08X)\n", (unsigned)bar);
    return true;
}

static void ac97_set_volume(int pct) {
    if (!s_ac97) return;
    uint32_t v = 31u - (uint32_t)(pct * 31u / 100u);
    if (v > 31u) v = 31u;
    mmio_wr32(AC97_PCM_VOL, (v << 8) | v);
}

static bool ac97_submit(const int16_t* pcm, uint32_t frames) {
    if (!s_ac97 || s_ac97_playing) return false;
    uint32_t bytes = frames * 4u;
    if (bytes > AC97_BLOCK_BYTES) bytes = AC97_BLOCK_BYTES;
    memcpy(s_ac97_buf, pcm, bytes);
    // BDL[0]: address + length-1
    uint32_t* bd = (uint32_t*)s_ac97_bdl;
    bd[0] = (uint32_t)(uintptr_t)s_ac97_buf;
    bd[1] = (uint32_t)((bytes - 1) & 0x3FFFu);
    if (!s_ac97_bdl_written) {
        mmio_wr32(AC97_BDL_BASE, (uint32_t)(uintptr_t)s_ac97_bdl);
        s_ac97_bdl_written = true;
    }
    mmio_wr32(AC97_LVI, 0);
    // restart: stop then run
    mmio_wr32(AC97_BM_CTRL, 0x0000u);
    mmio_wr32(AC97_BM_CTRL, 0x0002u);
    s_ac97_playing = true;
    s_ac97_block_bytes = bytes;
    return true;
}

static uint32_t ac97_pump(void) {
    if (!s_ac97 || !s_ac97_playing) return 0;
    uint32_t st = mmio_rd32(AC97_BM_STATUS);
    if (st & 0x0001u) {                       // ring empty -> block finished
        s_ac97_playing = false;
        uint32_t fr = s_ac97_block_bytes / 4u;
        s_played_frames += fr;
        return fr;
    }
    return 0;
}

static void ac97_stop(void) {
    if (!s_ac97) return;
    mmio_wr32(AC97_BM_CTRL, 0x0000u);
    s_ac97_playing = false;
}

// ===================== Intel HD Audio =====================
#define HDA_GCTL     0x08u
#define HDA_STATESTS 0x0Eu
#define HDA_CORBLBASE 0x40u
#define HDA_CORBUBASE 0x44u
#define HDA_CORBWP   0x48u
#define HDA_CORBRP   0x4Au
#define HDA_CORBCTL  0x4Cu
#define HDA_CORBSIZE 0x4Eu
#define HDA_RIRBLBASE 0x50u
#define HDA_RIRBUBASE 0x54u
#define HDA_RIRBWP   0x58u
#define HDA_RIRBCTL  0x5Cu
#define HDA_RIRBSIZE 0x5Eu
// SD0 stream descriptor (base 0x80)
#define HDA_SD0CTL   0x80u
#define HDA_SD0STS   0x84u
#define HDA_SD0LPIB  0x88u
#define HDA_SD0CBL   0x8Cu
#define HDA_SD0LVI   0x90u
#define HDA_SD0FMT   0x96u
#define HDA_SD0BDLPL 0x98u
#define HDA_SD0BDLPU 0x9Cu

#define HDA_RING_ENTRIES 64u        // 2 KB CORB/RIRB
#define HDA_BDL_ALIGN    128u

// verbs
#define HDA_GET_PARAM         0xF00u
#define HDA_SET_STREAM_FMT    0x200u
#define HDA_SET_PIN_WCTRL     0x707u
#define HDA_SET_AMP           0x300u

static volatile uint32_t* s_hda       = 0;   // BAR0
static uint32_t* s_hda_corb           = 0;   // 2 KB, 4K aligned
static uint32_t* s_hda_rirb           = 0;
static uint16_t  s_hda_corbwp         = 0;
static uint8_t*  s_hda_bdl            = 0;   // 128-byte aligned
static uint8_t*  s_hda_buf            = 0;   // 8 KB DMA buffer
static bool      s_hda_playing        = false;
static uint32_t  s_hda_block_bytes    = 0;

static inline uint32_t hda_rd(uint32_t reg) { return s_hda[reg / 4u]; }
static inline void hda_wr(uint32_t reg, uint32_t v) { s_hda[reg / 4u] = v; }

// send one verb to codec 0 and wait for the RIRB response
static uint32_t hda_verb(uint32_t nid, uint32_t verb, uint32_t payload);

static bool hda_probe_and_init(void) {
    static const uint16_t hda_ids[10] = {
        0x2668, 0x27D8, 0x293E, 0x3A3E, 0x3B56, 0x1C20, 0x1E20, 0x8CA0, 0x9CA0, 0x2804
    };
    PciDev list[64];
    int n = pci_scan(list, 64);
    PciDev dev;
    dev.valid = false;
    for (int i = 0; i < n; i++) {
        if (list[i].vendor == 0x8086) {
            for (int k = 0; k < 10; k++) {
                if (list[i].device == hda_ids[k]) { dev = list[i]; break; }
            }
        }
        if (dev.valid) break;
    }
    klogf("audio: hda scan: %d devices, match=%d\n", n, dev.valid ? 1 : 0);
    if (!dev.valid) return false;

    bool is_io = false;
    uint64_t bar = pci_bar_address(dev, 0, &is_io);
    klogf("audio: hda bar0=0x%08X io=%d\n", (unsigned)bar, is_io ? 1 : 0);
    if (!bar || is_io) return false;
    pci_enable_busmaster(dev);
    s_hda = (volatile uint32_t*)(uintptr_t)bar;

    // global reset: CRST 1 -> wait -> CRST 0 -> wait
    hda_wr(HDA_GCTL, 0x00000001u);
    for (uint32_t i = 0; i < 200000u; i++) pci_io_wait();
    hda_wr(HDA_GCTL, 0x00000000u);
    for (uint32_t i = 0; i < 200000u; i++) pci_io_wait();
    if (hda_rd(HDA_GCTL) & 1u) { s_hda = 0; return false; }

    // clear STATESTS (a codec must have responded after reset)
    uint32_t sts = hda_rd(HDA_STATESTS);
    hda_wr(HDA_STATESTS, sts);
    klogf("audio: hda statests=0x%04X\n", sts);
    if (!(sts & 0x0001u)) { s_hda = 0; return false; }   // no codec on SDI0

    // CORB / RIRB buffers (2 KB each, page aligned)
    uint8_t* corb_raw = (uint8_t*)kalloc(HDA_RING_ENTRIES * 4u + 4096u);
    uintptr_t corb = ((uintptr_t)corb_raw + 4095u) & ~(uintptr_t)4095u;
    uint8_t* rirb_raw = (uint8_t*)kalloc(HDA_RING_ENTRIES * 8u + 4096u);
    uintptr_t rirb = ((uintptr_t)rirb_raw + 4095u) & ~(uintptr_t)4095u;
    s_hda_corb = (uint32_t*)corb;
    s_hda_rirb = (uint32_t*)rirb;
    memset(s_hda_corb, 0, HDA_RING_ENTRIES * 4u);
    memset(s_hda_rirb, 0, HDA_RING_ENTRIES * 8u);

    // CORB setup (size=2 -> 2 KB)
    hda_wr(HDA_CORBSIZE, 0x00000002u);
    hda_wr(HDA_CORBLBASE, (uint32_t)(corb & 0xFFFFFFFFu));
    hda_wr(HDA_CORBUBASE, 0);
    hda_wr(HDA_CORBRP, 0);
    hda_wr(HDA_CORBCTL, 0x00000001u);   // DMA enable
    s_hda_corbwp = 0;
    hda_wr(HDA_CORBWP, 0);
    // RIRB setup
    hda_wr(HDA_RIRBSIZE, 0x00000002u);
    hda_wr(HDA_RIRBLBASE, (uint32_t)(rirb & 0xFFFFFFFFu));
    hda_wr(HDA_RIRBUBASE, 0);
    hda_wr(HDA_RIRBWP, 0);
    hda_wr(HDA_RIRBCTL, 0x00000000u);   // response IRQ off (polled)

    // ---- codec enumeration (codec 0) ----
    // NID 0 subnode count -> audio function group
    uint32_t snc = hda_verb(0x00, HDA_GET_PARAM, 0x04u);
    uint32_t afg = snc & 0xFFu;                 // first subnode = AFG
    if (afg == 0) { hda_wr(HDA_CORBCTL, 0); s_hda = 0; return false; }
    // AFG subnode count -> widgets
    uint32_t afg_snc = hda_verb(afg, HDA_GET_PARAM, 0x04u);
    uint32_t wstart = afg_snc & 0xFFu;
    uint32_t wcount = (afg_snc >> 8) & 0xFFu;
    if (wcount == 0 || wcount > 32) { hda_wr(HDA_CORBCTL, 0); s_hda = 0; return false; }

    // find the first audio-output (DAC) widget and the first pin widget
    uint32_t dac = 0, pin = 0;
    for (uint32_t w = 0; w < wcount; w++) {
        uint32_t nid = wstart + w;
        uint32_t caps = hda_verb(nid, HDA_GET_PARAM, 0x05u);   // audio widget caps
        uint32_t type = (caps >> 20) & 0x0Fu;
        if (type == 0x0 && dac == 0) dac = nid;     // output (DAC)
        else if (type == 0x4 && pin == 0) pin = nid; // pin complex
    }
    if (dac == 0) { hda_wr(HDA_CORBCTL, 0); s_hda = 0; return false; }

    // configure the pin to output + route the DAC
    if (pin) {
        hda_verb(pin, HDA_SET_PIN_WCTRL, 0x0040u);   // PIN_OUT
        hda_verb(pin, 0x701u, 0);                    // SET_CONNECTION_SELECT -> first input
    }
    // DAC: unmute output amp, set stream format 44100/16/stereo = 0x0611
    hda_verb(dac, HDA_SET_AMP, 0x0000u);             // gain 0, unmuted
    hda_verb(dac, HDA_SET_STREAM_FMT, 0x0611u);

    // BDL + DMA buffer
    s_hda_bdl = (uint8_t*)kalloc(HDA_BDL_ALIGN + 16u);
    uintptr_t bdl = ((uintptr_t)s_hda_bdl + (HDA_BDL_ALIGN - 1)) & ~(uintptr_t)(HDA_BDL_ALIGN - 1);
    s_hda_buf = (uint8_t*)kalloc(AC97_BLOCK_BYTES + 64u);
    uintptr_t buf = ((uintptr_t)s_hda_buf + 63u) & ~(uintptr_t)63u;
    s_hda_bdl = (uint8_t*)bdl;
    s_hda_buf = (uint8_t*)buf;
    // BDL[0]: low dword = address, high dword = (len-1) | high address
    uint32_t* hb = (uint32_t*)bdl;
    hb[0] = (uint32_t)buf;
    hb[1] = (uint32_t)((AC97_BLOCK_BYTES - 1) & 0xFFFFu) | ((uint32_t)((buf >> 32) & 0xFFFFu) << 16);

    hda_wr(HDA_SD0LVI, 0);
    hda_wr(HDA_SD0FMT, 0x0611u);
    hda_wr(HDA_SD0BDLPL, (uint32_t)(bdl & 0xFFFFFFFFu));
    hda_wr(HDA_SD0BDLPU, (uint32_t)(bdl >> 32));
    s_hda_playing = false;
    klogf("audio: Intel HD Audio ready (dac 0x%X pin 0x%X)\n", dac, pin);
    return true;
}

// send one verb to codec 0 and wait for the RIRB response
static uint32_t hda_verb(uint32_t nid, uint32_t verb, uint32_t payload) {
    if (!s_hda) return 0;
    uint32_t cmd = (nid << 20) | (verb << 8) | (payload & 0xFFu);
    // wait for CORB room: device consumes when CORBRP follows CORBWP
    uint32_t wait = 0;
    for (;;) {
        uint32_t rp = hda_rd(HDA_CORBRP) & 0xFFu;
        if (rp == (uint32_t)(s_hda_corbwp & 0xFFu)) break;
        if (++wait > 2000000u) return 0;
    }
    s_hda_corb[s_hda_corbwp % HDA_RING_ENTRIES] = cmd;
    s_hda_corbwp = (uint16_t)((s_hda_corbwp + 1) & 0xFFu);
    hda_wr(HDA_CORBWP, s_hda_corbwp);
    // wait for the response in RIRB
    uint32_t base = hda_rd(HDA_RIRBWP) & 0x7Fu;
    wait = 0;
    uint32_t wp = base;
    for (;;) {
        wp = hda_rd(HDA_RIRBWP) & 0x7Fu;
        if (wp != base) break;
        if (++wait > 4000000u) return 0;
    }
    uint32_t idx = (wp - 1u) % HDA_RING_ENTRIES;
    return s_hda_rirb[idx * 2u];                 // response dword of the entry
}

static void hda_set_volume(int pct) {
    // volume is applied on the DAC output amp; find the DAC again on demand
    if (!s_hda) return;
    // re-scan the AFG widgets quickly (cheap, keeps state stateless)
    uint32_t snc = hda_verb(0x00, HDA_GET_PARAM, 0x04u);
    uint32_t afg = snc & 0xFFu;
    if (!afg) return;
    uint32_t afg_snc = hda_verb(afg, HDA_GET_PARAM, 0x04u);
    uint32_t wstart = afg_snc & 0xFFu;
    uint32_t wcount = (afg_snc >> 8) & 0xFFu;
    if (wcount > 32) wcount = 32;
    for (uint32_t w = 0; w < wcount; w++) {
        uint32_t nid = wstart + w;
        uint32_t caps = hda_verb(nid, HDA_GET_PARAM, 0x05u);
        if (((caps >> 20) & 0x0Fu) == 0x0) {
            uint32_t gain = 127u - (uint32_t)(pct * 127u / 100u);
            if (gain > 127u) gain = 127u;
            hda_verb(nid, HDA_SET_AMP, gain);   // output amp, unmuted
            break;
        }
    }
}

static bool hda_submit(const int16_t* pcm, uint32_t frames) {
    if (!s_hda || s_hda_playing) return false;
    uint32_t bytes = frames * 4u;
    if (bytes > AC97_BLOCK_BYTES) bytes = AC97_BLOCK_BYTES;
    memcpy(s_hda_buf, pcm, bytes);
    uint32_t* hb = (uint32_t*)s_hda_bdl;
    hb[0] = (uint32_t)(uintptr_t)s_hda_buf;
    hb[1] = (uint32_t)((bytes - 1) & 0xFFFFu) | ((uint32_t)((uintptr_t)s_hda_buf >> 32) << 16);
    // restart the stream: clear RUN, then set RUN + tag 1
    hda_wr(HDA_SD0CTL, 0x00000001u);            // tag 1, RUN off
    hda_wr(HDA_SD0LVI, 0);
    hda_wr(HDA_SD0CTL, 0x00000011u);            // RUN + tag 1
    s_hda_playing = true;
    s_hda_block_bytes = bytes;
    return true;
}

static uint32_t hda_pump(void) {
    if (!s_hda || !s_hda_playing) return 0;
    uint32_t sts = hda_rd(HDA_SD0STS);
    if (sts & 0x10u) {                            // BCIS: buffer complete
        hda_wr(HDA_SD0STS, 0x10u);                // clear
        hda_wr(HDA_SD0CTL, 0x00000001u);          // RUN off
        s_hda_playing = false;
        uint32_t fr = s_hda_block_bytes / 4u;
        s_played_frames += fr;
        return fr;
    }
    return 0;
}

static void hda_stop(void) {
    if (!s_hda) return;
    hda_wr(HDA_SD0CTL, 0x00000001u);              // RUN off
    s_hda_playing = false;
}

// ===================== public API =====================

int audio_probe(void) {
    if (s_backend != AUDIO_NONE) return s_backend;
    if (ac97_probe_and_init()) {
        s_backend = AUDIO_AC97;
        s_rate = 44100;
        s_channels = 2;
        ac97_set_volume(s_volume);
        return s_backend;
    }
    if (hda_probe_and_init()) {
        s_backend = AUDIO_HDA;
        s_rate = 44100;
        s_channels = 2;
        hda_set_volume(s_volume);
        return s_backend;
    }
    s_backend = AUDIO_NONE;
    return s_backend;
}

const char* audio_backend_name(void) {
    switch (s_backend) {
        case AUDIO_AC97: return "Intel AC'97";
        case AUDIO_HDA:  return "Intel HD Audio";
        default:         return "None";
    }
}

bool audio_available(void) { return s_backend != AUDIO_NONE; }

bool audio_output_start(uint32_t rate, uint8_t channels) {
    if (s_backend == AUDIO_NONE) return false;
    s_rate = rate ? rate : 44100u;
    s_channels = channels ? channels : 2u;
    s_played_frames = 0;
    // AC'97: program the sample rate register
    if (s_backend == AUDIO_AC97) mmio_wr32(AC97_PCM_RATE, s_rate);
    return true;
}

void audio_output_stop(void) {
    if (s_backend == AUDIO_AC97) ac97_stop();
    else if (s_backend == AUDIO_HDA) hda_stop();
}

bool audio_output_submit(const int16_t* pcm, uint32_t frames) {
    if (!pcm || frames == 0) return false;
    if (frames > AUDIO_OUTPUT_BLOCK_FRAMES) frames = AUDIO_OUTPUT_BLOCK_FRAMES;
    if (s_backend == AUDIO_AC97) return ac97_submit(pcm, frames);
    if (s_backend == AUDIO_HDA) return hda_submit(pcm, frames);
    return false;
}

uint32_t audio_output_pump(void) {
    if (s_backend == AUDIO_AC97) return ac97_pump();
    if (s_backend == AUDIO_HDA) return hda_pump();
    return 0;
}

uint32_t audio_output_played_frames(void) { return s_played_frames; }

bool audio_output_busy(void) {
    if (s_backend == AUDIO_AC97) return s_ac97_playing;
    if (s_backend == AUDIO_HDA) return s_hda_playing;
    return false;
}

void audio_output_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume = percent;
    if (s_backend == AUDIO_AC97) ac97_set_volume(percent);
    else if (s_backend == AUDIO_HDA) hda_set_volume(percent);
}

} // namespace audio
} // namespace drivers
} // namespace nefu
