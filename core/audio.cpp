// nefuOS audio subsystem (core, backend-independent)
// Shared helpers on top of the platform audio API:
//   platform_play_wav_mem  - play an in-memory RIFF/WAVE buffer
//   platform_play_wav_path - load a WAV from the VFS, then play it
//   platform_audio_available - true when a sound device was probed
// The bare-metal backend drives a real Sound Blaster 16 (DSP + 8237 DMA);
// the Win32 host uses the Windows PlaySound API.
#include "platform.h"
#include "vfs/vfs.h"

namespace nefu {

// Load a WAV file from the virtual file system and hand it to the backend.
// Used by apps (Music Player) and by the bare backend's platform_play_wav.
bool platform_play_wav_path(const char* path) {
    if (!path || !g_vfs) return false;
    FSNode* f = g_vfs->resolve(path);
    if (!f || f->is_dir || !f->data || f->size < 44) return false;
    return platform_play_wav_mem(f->data, f->size);
}

} // namespace nefu
