// nefuOS Win32 media engine — REAL audio/video playback for the host backend.
//
// Uses Windows Media Foundation (Source Reader) to decode whatever the OS
// codecs provide — WAV/MP3/FLAC/AAC/M4A/WMA audio, MP4/MOV/AVI/WMV/MPG/3GP
// video (H.264, MPEG-4, MJPEG, WMV, MPEG-1/2, ...) — then streams PCM out
// through waveOut and hands decoded RGB32 video frames to the core video
// player. The bare-metal backend stubs these out and the apps fall back to
// their demo content.
//
// Threads: a single decode thread drives IMFSourceReader.ReadSample for both
// streams (video frames -> shared RGB32 buffer; audio PCM -> waveOut blocks).
// The UI thread only calls the small lock-protected accessors below.
#include <winsock2.h>
#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <wtypes.h>
#include <propidl.h>
#include <cstdio>
#include <cstring>
#include "../../core/klib/klib.h"
#include "../../core/platform.h"

// Mingw-w64 does not ship MF_SOURCE_READER_MEDIATYPE; we avoid it anyway and
// read the duration through the media source -> presentation descriptor.
#define MF_SOURCE_READER_MEDIATYPE 0xFFFFFFFA

// Mingw-w64's mfapi.h lacks MF_MT_AUDIO_SAMPLE_RATE (it exists in the Windows
// SDK); supply the attribute GUID here so the audio rate can be queried.
#ifndef MF_MT_AUDIO_SAMPLE_RATE
static const GUID kMfMtAudioSampleRate =
    {0x6326FBAA, 0x8D51, 0x4FBF, {0x8E, 0xF3, 0x35, 0x2D, 0x7B, 0xE0, 0xD6, 0x34}};
#define MF_MT_AUDIO_SAMPLE_RATE kMfMtAudioSampleRate
#endif

// MF_MT_FRAME_SIZE is a UINT64 attribute (upper 32 bits = width, lower 32 =
// height) despite what older samples suggest; it must be read with GetUINT64
// (or MFGetAttributeSize), not GetUINT32 — see
// https://learn.microsoft.com/en-us/windows/win32/medfound/mf-mt-frame-size-attribute
static inline bool get_frame_size(IMFMediaType* mt, int& w, int& h) {
    UINT64 packed = 0;
    if (!mt || FAILED(mt->GetUINT64(MF_MT_FRAME_SIZE, &packed)) || packed == 0) return false;
    w = (int)(packed >> 32);
    h = (int)(packed & 0xFFFFFFFFu);
    return w > 0 && h > 0;
}

namespace nefu {
namespace media_win32 {
// MinGW-w64 declares GUID_NULL as an import from ole32 but the import
// resolution is unreliable on this toolchain; use a static zero GUID instead.
static const GUID kGuidNull = {0,0,0,{0,0,0,0,0,0,0,0}};
#define GUID_NULL kGuidNull


template<class T> static inline void SafeRelease(T*& p) {
    if (p) { p->Release(); p = NULL; }
}

// ===================== global engine state =====================
static bool s_mf_ok = false;
static bool s_startup_done = false;

static bool mf_ensure() {
    if (s_startup_done) return s_mf_ok;
    s_startup_done = true;
    // COM must be up before any MF object is created; MTA keeps the objects
    // usable from the decode thread as well.
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    (void)hrCo;   // RPC_E_CHANGED_MODE (already STA) is not fatal for MF MTA use
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    s_mf_ok = SUCCEEDED(hr);
    if (!s_mf_ok) platform_dbg("media: MFStartup failed\n");
    return s_mf_ok;
}

// ===================== waveOut PCM writer =====================
enum { MW_NBLOCKS = 8, MW_BLOCK_BYTES = 8192 };

struct WaveOut {
    HWAVEOUT   h;
    bool       ok;
    uint32_t   rate, channels, bits, byte_rate;
    HANDLE     done_evt;
    WAVEHDR    blocks[MW_NBLOCKS];
    uint8_t*   data[MW_NBLOCKS];
    int        inflight;

    WaveOut() : h(0), ok(false), rate(0), channels(0), bits(0), byte_rate(0),
                done_evt(0), inflight(0) {
        for (int i = 0; i < MW_NBLOCKS; i++) { memset(&blocks[i], 0, sizeof(WAVEHDR)); data[i] = 0; }
    }

    bool open(uint32_t r, uint32_t ch, uint32_t b) {
        rate = r; channels = ch; bits = b;
        byte_rate = r * ch * (b / 8);
        done_evt = CreateEventA(0, FALSE, FALSE, 0);
        if (!done_evt) return false;
        WAVEFORMATEX wf;
        memset(&wf, 0, sizeof(wf));
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = (WORD)ch;
        wf.nSamplesPerSec = r;
        wf.wBitsPerSample = (WORD)b;
        wf.nBlockAlign = (WORD)(ch * b / 8);
        wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
        MMRESULT mr = waveOutOpen(&h, WAVE_MAPPER, &wf, (DWORD_PTR)done_evt, 0, CALLBACK_EVENT);
        if (mr != MMSYSERR_NOERROR) { CloseHandle(done_evt); done_evt = 0; return false; }
        for (int i = 0; i < MW_NBLOCKS; i++) {
            data[i] = (uint8_t*)kalloc(MW_BLOCK_BYTES);
            blocks[i].lpData = (LPSTR)data[i];
            blocks[i].dwBufferLength = MW_BLOCK_BYTES;
            waveOutPrepareHeader(h, &blocks[i], sizeof(WAVEHDR));
        }
        ok = true;
        return true;
    }

    // Reclaim blocks whose playback finished.
    void collect() {
        for (int i = 0; i < MW_NBLOCKS && inflight > 0; i++) {
            if (blocks[i].dwFlags & WHDR_DONE) {
                blocks[i].dwFlags &= ~WHDR_DONE;
                blocks[i].dwUser = 0;   // reclaim: block becomes reusable
                inflight--;
            }
        }
    }

    void write(const uint8_t* pcm, uint32_t len) {
        if (!ok) return;
        while (len > 0) {
            if (inflight >= MW_NBLOCKS) {
                WaitForSingleObject(done_evt, 50);
                collect();
                continue;
            }
            int bi = -1;
            for (int i = 0; i < MW_NBLOCKS; i++) {
                if (!(blocks[i].dwFlags & WHDR_DONE) && blocks[i].dwUser == 0) { bi = i; break; }
            }
            if (bi < 0) { collect(); continue; }
            blocks[bi].dwUser = 1;
            blocks[bi].dwBufferLength = MW_BLOCK_BYTES;
            uint32_t copy = len < (uint32_t)MW_BLOCK_BYTES ? len : (uint32_t)MW_BLOCK_BYTES;
            memcpy(data[bi], pcm, copy);
            blocks[bi].dwBufferLength = copy;
            if (waveOutWrite(h, &blocks[bi], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                blocks[bi].dwUser = 0;
                blocks[bi].dwFlags &= ~WHDR_DONE;
                break;   // device gone (e.g. unplugged) — drop the rest
            }
            blocks[bi].dwUser = 2;   // in-flight marker
            inflight++;
            pcm += copy;
            len -= copy;
        }
    }

    void reset() {
        if (!ok) return;
        waveOutReset(h);
        collect();
        for (int i = 0; i < MW_NBLOCKS; i++) { blocks[i].dwUser = 0; blocks[i].dwFlags &= ~WHDR_DONE; }
        inflight = 0;
    }

    void close() {
        if (!ok) return;
        waveOutReset(h);
        for (int i = 0; i < MW_NBLOCKS; i++) {
            waveOutUnprepareHeader(h, &blocks[i], sizeof(WAVEHDR));
            if (data[i]) { kfree(data[i]); data[i] = 0; }
        }
        waveOutClose(h);
        h = 0;
        if (done_evt) { CloseHandle(done_evt); done_evt = 0; }
        ok = false;
    }
};

// ===================== media player state =====================
struct MediaPlayer {
    IMFSourceReader* reader;
    HANDLE thread;
    HANDLE wake;
    CRITICAL_SECTION cs;

    volatile bool stop_flag;
    volatile bool pause_flag;
    volatile bool seek_pending;
    LONGLONG      seek_100ns;

    int video_idx;                 // -1 = none
    int audio_idx;                 // -1 = none
    bool playing;

    // audio
    WaveOut wave;
    uint64_t bytes_fed;            // PCM bytes written since open/seek

    // video
    uint8_t* frame;                // RGB32 w*h*4 (BGRA memory order)
    int      frame_w, frame_h;
    bool     frame_ready;
    LONGLONG first_vts;            // first video sample timestamp (100ns)
    bool     have_first_vts;
    uint32_t play_start_ms;

    // shared status
    uint64_t pos_ms;               // current position (updated by decode thread)
    uint64_t dur_100ns;            // 0 = unknown
    bool     has_video;

    MediaPlayer() : reader(0), thread(0), wake(0), stop_flag(false),
                    pause_flag(false), seek_pending(false), seek_100ns(0),
                    video_idx(-1), audio_idx(-1), playing(false), bytes_fed(0),
                    frame(0), frame_w(0), frame_h(0), frame_ready(false),
                    first_vts(0), have_first_vts(false), play_start_ms(0),
                    pos_ms(0), dur_100ns(0), has_video(false) {
        InitializeCriticalSection(&cs);
    }

    void lock()   { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
};

static MediaPlayer s_p;

// ===================== decode thread =====================
static wchar_t* to_wide(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, 0, 0);
    if (n <= 0) return 0;
    wchar_t* w = (wchar_t*)kalloc((size_t)(n + 1) * sizeof(wchar_t));
    if (!w) return 0;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void store_video_frame(IMFSample* samp) {
    if (s_p.frame_w <= 0 || s_p.frame_h <= 0) return;
    IMFMediaBuffer* buf = 0;
    if (FAILED(samp->ConvertToContiguousBuffer(&buf))) return;
    uint8_t* ptr = 0;
    DWORD len = 0, maxlen = 0;
    if (SUCCEEDED(buf->Lock(&ptr, &maxlen, &len))) {
        size_t need = (size_t)s_p.frame_w * (size_t)s_p.frame_h * 4;
        if (ptr && len >= need) {
            s_p.lock();
            memcpy(s_p.frame, ptr, need);
            s_p.frame_ready = true;
            s_p.unlock();
        }
        buf->Unlock();
    }
    buf->Release();
}

static void do_seek() {
    s_p.lock();
    s_p.seek_pending = false;
    s_p.unlock();
    PROPVARIANT var;
    memset(&var, 0, sizeof(var));
    var.vt = VT_I8;
    var.hVal.QuadPart = s_p.seek_100ns;
    if (s_p.reader) s_p.reader->SetCurrentPosition(GUID_NULL, var);
    s_p.lock();
    s_p.bytes_fed = 0;
    s_p.have_first_vts = false;
    s_p.pos_ms = (uint64_t)(s_p.seek_100ns / 10000);
    s_p.unlock();
    if (s_p.wave.ok) s_p.wave.reset();
}

static DWORD WINAPI decode_thread(LPVOID) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool video_done = false, audio_done = false;
    s_p.play_start_ms = (uint32_t)GetTickCount();

    for (;;) {
        if (s_p.stop_flag) break;

        // pause gate (seek requests are honoured while paused too)
        while (s_p.pause_flag && !s_p.stop_flag) {
            if (s_p.seek_pending) do_seek();
            WaitForSingleObject(s_p.wake, 20);
        }
        if (s_p.stop_flag) break;
        if (s_p.seek_pending) do_seek();
        if (!s_p.playing && !s_p.pause_flag) { WaitForSingleObject(s_p.wake, 20); continue; }

        bool progress = false;

        // ---- video stream ----
        if (!video_done && s_p.video_idx >= 0 && s_p.reader) {
            DWORD sflags = 0, sfl = 0;
            LONGLONG ts = 0;
            IMFSample* samp = 0;
            HRESULT hr = s_p.reader->ReadSample((DWORD)s_p.video_idx, 0, &sflags, &sfl, &ts, &samp);
            if (SUCCEEDED(hr) && samp) {
                if (ts >= 0) {
                    if (!s_p.have_first_vts) {
                        s_p.have_first_vts = true;
                        s_p.first_vts = ts;
                        s_p.play_start_ms = (uint32_t)GetTickCount();
                    }
                    // pace video to its timestamps (keeps silent clips watchable)
                    LONGLONG due = (ts - s_p.first_vts) / 10000;
                    uint32_t elapsed = (uint32_t)GetTickCount() - s_p.play_start_ms;
                    if (due > elapsed + 12) Sleep((DWORD)(due - elapsed));
                    s_p.lock();
                    s_p.pos_ms = (uint64_t)(due > 0 ? due : 0);
                    s_p.unlock();
                }
                store_video_frame(samp);
                samp->Release();
                progress = true;
            } else if (SUCCEEDED(hr)) {
                // sample == NULL
            }
            if (sflags & MF_SOURCE_READERF_ENDOFSTREAM) video_done = true;
        }

        // ---- audio stream ----
        if (!audio_done && s_p.audio_idx >= 0 && s_p.reader && s_p.wave.ok) {
            DWORD sflags = 0, sfl = 0;
            LONGLONG ts = 0;
            IMFSample* samp = 0;
            HRESULT hr = s_p.reader->ReadSample((DWORD)s_p.audio_idx, 0, &sflags, &sfl, &ts, &samp);
            if (SUCCEEDED(hr) && samp) {
                IMFMediaBuffer* buf = 0;
                if (SUCCEEDED(samp->GetBufferByIndex(0, &buf))) {
                    uint8_t* ptr = 0;
                    DWORD len = 0, maxlen = 0;
                    if (SUCCEEDED(buf->Lock(&ptr, &maxlen, &len)) && ptr && len) {
                        s_p.wave.write(ptr, len);
                        s_p.lock();
                        s_p.bytes_fed += len;
                        s_p.pos_ms = s_p.wave.byte_rate ? s_p.bytes_fed * 1000 / s_p.wave.byte_rate : 0;
                        s_p.unlock();
                        buf->Unlock();
                    }
                    buf->Release();
                }
                samp->Release();
                progress = true;
            }
            if (sflags & MF_SOURCE_READERF_ENDOFSTREAM) audio_done = true;
        }

        if ((s_p.video_idx < 0 || video_done) && (s_p.audio_idx < 0 || audio_done || !s_p.wave.ok)) {
            // natural end: hold the last frame, report EOF position
            s_p.lock();
            s_p.playing = false;
            if (s_p.dur_100ns) s_p.pos_ms = s_p.dur_100ns / 10000;
            s_p.unlock();
            if (s_p.wave.ok) s_p.wave.reset();
            break;
        }
        if (!progress) Sleep(2);
    }

    CoUninitialize();
    return 0;
}

// ===================== open a media file =====================
void close_media();   // defined below (also used by open_media)

static bool set_media_type(int idx, const GUID& major, const GUID& sub) {
    IMFMediaType* mt = 0;
    if (FAILED(MFCreateMediaType(&mt))) return false;
    mt->SetGUID(MF_MT_MAJOR_TYPE, major);
    mt->SetGUID(MF_MT_SUBTYPE, sub);
    if (major == MFMediaType_Audio)
        mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);   // waveOut-friendliest
    HRESULT hr = s_p.reader->SetCurrentMediaType((DWORD)idx, NULL, mt);
    mt->Release();
    return SUCCEEDED(hr);
}

static bool open_media(const char* host_path, bool audio_only) {
    close_media();

    if (!mf_ensure()) return false;
    wchar_t* wpath = to_wide(host_path);
    if (!wpath) return false;

    IMFSourceReader* reader = 0;
    // Enable the VideoProcessorMFT so the Source Reader can convert arbitrary
    // decoded video (NV12 etc. from H.264/MPEG-4...) to RGB32 for the core
    // video player. Without this, RGB32 output type only works for sources
    // that already decode to RGB (e.g. MJPEG) and MP4/H.264 would drop video.
    IMFAttributes* attrs = 0;
    if (SUCCEEDED(MFCreateAttributes(&attrs, 2)))
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    HRESULT hr = MFCreateSourceReaderFromURL(wpath, attrs, &reader);
    SafeRelease(attrs);
    kfree(wpath);
    if (FAILED(hr)) {
        platform_dbg("media: open failed\n");
        return false;
    }
    s_p.reader = reader;

    // find streams
    int v = -1, a = -1;
    for (DWORD i = 0; i < 8; i++) {
        IMFMediaType* mt = 0;
        if (FAILED(reader->GetCurrentMediaType(i, &mt))) break;
        GUID major = GUID_NULL;
        mt->GetGUID(MF_MT_MAJOR_TYPE, &major);
        mt->Release();
        if (major == MFMediaType_Video && v < 0) v = (int)i;
        else if (major == MFMediaType_Audio && a < 0) a = (int)i;
        if (v >= 0 && a >= 0) break;
    }

    // video file: decode video + (optional) audio; audio file: audio only
    if (audio_only) {
        if (a < 0) { close_media(); return false; }
        if (v >= 0) reader->SetStreamSelection((DWORD)v, FALSE);
        reader->SetStreamSelection((DWORD)a, TRUE);
    } else {
        if (v < 0 && a < 0) { close_media(); return false; }
        if (v >= 0) { reader->SetStreamSelection((DWORD)v, TRUE); }
        if (a >= 0) { reader->SetStreamSelection((DWORD)a, TRUE); }
    }

    if (v >= 0) {
        // Capture the decoder's native frame size BEFORE switching the output
        // type to RGB32: with Advanced Video Processing active the converted
        // output type often has no MF_MT_FRAME_SIZE attribute.
        IMFMediaType* nat = 0;
        if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)v, &nat))) {
            get_frame_size(nat, s_p.frame_w, s_p.frame_h);
            nat->Release();
        }
        if (!set_media_type(v, MFMediaType_Video, MFVideoFormat_RGB32)) v = -1;
        else if (s_p.frame_w <= 0 || s_p.frame_h <= 0) {
            // fallback: some pipelines do report the size on the RGB32 type
            IMFMediaType* mt = 0;
            if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)v, &mt))) {
                get_frame_size(mt, s_p.frame_w, s_p.frame_h);
                mt->Release();
            }
        }
    }
    if (a >= 0 && set_media_type(a, MFMediaType_Audio, MFAudioFormat_PCM)) {
        IMFMediaType* mt = 0;
        if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)a, &mt))) {
            UINT32 rate = 44100, ch = 2, bits = 16;
            mt->GetUINT32(MF_MT_AUDIO_SAMPLE_RATE, &rate);
            mt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
            mt->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
            mt->Release();
            if (rate > 0 && ch > 0 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) {
                s_p.wave.open(rate, ch, bits);
            }
        }
    }

    s_p.video_idx = (v >= 0 && s_p.frame_w > 0 && s_p.frame_h > 0) ? v : -1;
    s_p.audio_idx = (a >= 0 && s_p.wave.ok) ? a : -1;
    s_p.has_video = s_p.video_idx >= 0;

    // duration (100ns) via the media source presentation descriptor
    IMFMediaSource* src = 0;
    if (SUCCEEDED(reader->GetServiceForStream(MF_SOURCE_READER_MEDIASOURCE, GUID_NULL,
                                              __uuidof(IMFMediaSource), (void**)&src))) {
        IMFPresentationDescriptor* pd = 0;
        if (SUCCEEDED(src->CreatePresentationDescriptor(&pd))) {
            UINT64 dur = 0;
            if (SUCCEEDED(pd->GetUINT64(MF_PD_DURATION, &dur))) s_p.dur_100ns = dur;
            pd->Release();
        }
        src->Release();
    }

    if (s_p.video_idx < 0 && s_p.audio_idx < 0) { close_media(); return false; }

    if (s_p.has_video && !s_p.frame) {
        size_t need = (size_t)s_p.frame_w * (size_t)s_p.frame_h * 4;
        s_p.frame = (uint8_t*)kalloc(need);
        if (!s_p.frame) { close_media(); return false; }
    }

    s_p.wake = CreateEventA(0, FALSE, FALSE, 0);
    s_p.stop_flag = false;
    s_p.pause_flag = false;
    s_p.seek_pending = false;
    s_p.bytes_fed = 0;
    s_p.pos_ms = 0;
    s_p.frame_ready = false;
    s_p.have_first_vts = false;

    s_p.thread = CreateThread(0, 0, decode_thread, 0, 0, 0);
    if (!s_p.thread) { close_media(); return false; }
    return true;
}

// ===================== public platform API =====================
void close_media() {
    s_p.stop_flag = true;
    if (s_p.wake) SetEvent(s_p.wake);
    if (s_p.thread) {
        WaitForSingleObject(s_p.thread, 2000);
        CloseHandle(s_p.thread);
        s_p.thread = 0;
    }
    s_p.pause_flag = false;
    s_p.seek_pending = false;
    s_p.playing = false;
    if (s_p.wake) { CloseHandle(s_p.wake); s_p.wake = 0; }
    if (s_p.wave.ok) s_p.wave.close();
    if (s_p.frame) { kfree(s_p.frame); s_p.frame = 0; }
    s_p.frame_w = s_p.frame_h = 0;
    s_p.frame_ready = false;
    s_p.bytes_fed = 0;
    s_p.pos_ms = 0;
    s_p.dur_100ns = 0;
    s_p.video_idx = s_p.audio_idx = -1;
    s_p.has_video = false;
    SafeRelease(s_p.reader);
}

} // namespace media_win32

// ---------------- platform glue (namespace nefu) ----------------
bool platform_media_available() { return media_win32::mf_ensure(); }

bool platform_media_open(const char* host_path, bool audio_only) {
    if (!host_path || !host_path[0]) return false;
    return media_win32::open_media(host_path, audio_only);
}

void platform_media_close() { media_win32::close_media(); }

bool platform_media_play() {
    using namespace media_win32;
    if (!s_p.reader) return false;
    s_p.lock();
    bool was_paused = s_p.pause_flag;
    s_p.pause_flag = false;
    s_p.playing = true;
    s_p.play_start_ms = (uint32_t)GetTickCount();
    s_p.have_first_vts = false;
    s_p.unlock();
    if (s_p.wave.ok && was_paused) waveOutRestart(s_p.wave.h);
    if (s_p.wake) SetEvent(s_p.wake);
    return true;
}

bool platform_media_pause() {
    using namespace media_win32;
    if (!s_p.reader) return false;
    s_p.lock();
    s_p.pause_flag = true;
    s_p.playing = false;
    s_p.unlock();
    if (s_p.wave.ok) waveOutPause(s_p.wave.h);
    if (s_p.wake) SetEvent(s_p.wake);
    return true;
}

void platform_media_stop() {
    using namespace media_win32;
    if (!s_p.reader) return;
    s_p.lock();
    s_p.pause_flag = true;
    s_p.playing = false;
    s_p.seek_100ns = 0;
    s_p.seek_pending = true;
    s_p.unlock();
    if (s_p.wave.ok) waveOutPause(s_p.wave.h);
    if (s_p.wake) SetEvent(s_p.wake);
}

bool platform_media_seek_sec(int sec) {
    using namespace media_win32;
    if (!s_p.reader || sec < 0) return false;
    s_p.lock();
    s_p.seek_100ns = (LONGLONG)sec * 10000000LL;
    s_p.seek_pending = true;
    s_p.unlock();
    if (s_p.wake) SetEvent(s_p.wake);
    return true;
}

int platform_media_position_sec() {
    using namespace media_win32;
    if (!s_p.reader) return 0;
    s_p.lock();
    uint64_t ms = s_p.pos_ms;
    s_p.unlock();
    return (int)(ms / 1000);
}

int platform_media_duration_sec() {
    using namespace media_win32;
    if (!s_p.reader || s_p.dur_100ns == 0) return -1;
    return (int)(s_p.dur_100ns / 10000000);
}

void platform_media_set_volume(int percent) {
    using namespace media_win32;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (s_p.wave.ok) {
        DWORD v = (DWORD)((unsigned)percent * 0xFFFFu / 100u);
        waveOutSetVolume(s_p.wave.h, v | (v << 16));
    }
}

bool platform_media_has_video() {
    using namespace media_win32;
    return s_p.reader && s_p.has_video;
}

bool platform_media_frame_info(int* w, int* h) {
    using namespace media_win32;
    s_p.lock();
    bool ok = s_p.frame_ready && s_p.frame_w > 0 && s_p.frame_h > 0;
    if (ok && w) *w = s_p.frame_w;
    if (ok && h) *h = s_p.frame_h;
    s_p.unlock();
    return ok;
}

bool platform_media_grab_frame(uint8_t* out_rgba) {
    using namespace media_win32;
    if (!out_rgba || !s_p.frame_ready) return false;
    s_p.lock();
    if (!s_p.frame_ready) { s_p.unlock(); return false; }
    memcpy(out_rgba, s_p.frame, (size_t)s_p.frame_w * (size_t)s_p.frame_h * 4);
    s_p.unlock();
    return true;
}

bool platform_host_file_dialog(char* out_path, int max, const char* filter_desc, const char* filter_pattern) {
    if (!out_path || max <= 0) return false;
    out_path[0] = 0;
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = 0;
    ofn.lpstrFilter = filter_desc ? filter_desc : "Media files\0*.*\0All files\0*.*\0\0";
    ofn.lpstrFile = out_path;
    ofn.nMaxFile = (DWORD)max;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Open media file";
    (void)filter_pattern;
    return GetOpenFileNameA(&ofn) != 0;
}

} // namespace nefu
