// nefuOS audio engine - integer-only facade over the real sound-card drivers.
// The heavy lifting (mixer math) lives in audio_manager.h / audio_driver.cpp,
// which are compiled with the FPU ABI; this module exposes a pure integer API
// so the bare-metal backend (compiled with -mgeneral-regs-only) can drive
// playback without touching float code.
//
//  engine_*   - init the sound card + periodic mix/submit pump
//  media_*    - a single controllable media session (WAV/PCM16) used by the
//               video player and the music player on bare metal
//  engine_play_wav_mem - one-shot blocking playback (platform_play_wav_mem)
#pragma once

#include <stdint.h>

namespace nefu {
namespace audio {

// ---- engine ----
bool engine_init(void);                 // probe AC'97 / HDA and start the engine
void engine_pump(void);                 // mix active streams -> submit (call each tick)
bool engine_available(void);            // a real sound card is driving output
const char* engine_device_name(void);   // "Intel AC'97" / "Intel HD Audio" / "None"

// ---- one-shot playback (blocks until the buffer finished) ----
bool engine_play_wav_mem(const uint8_t* data, uint32_t size);

// ---- media session (video player / music player) ----
bool media_available(void);                                  // sound card present
bool media_open_mem(const uint8_t* data, uint32_t size);     // open a WAV buffer
bool media_open_path(const char* vfs_path);                  // open a VFS WAV file
void media_close(void);
bool media_play(void);
void media_pause(void);
void media_stop(void);
bool media_seek_sec(int sec);
int  media_position_sec(void);
int  media_duration_sec(void);
void media_set_volume(int percent);
bool media_has_video(void);
bool media_get_info(uint32_t* rate, uint32_t* total_frames); // for progress/waveform

} // namespace audio
} // namespace nefu
