// nefuOS audio driver - unified sound-card output interface.
// Mainstream families implemented in core/drivers/audio_driver.cpp:
//   * Intel AC'97 (ICH family + VIA) - NABMBAR DMA, BDL ring
//   * Intel HD Audio (ICH6-10, 5/6/7 series) - CORB/RIRB + SD0 stream
// Block model: submit() a 16-bit stereo PCM block, pump() detects completion.
#pragma once

#include "../klib/klib.h"

namespace nefu {
namespace drivers {
namespace audio {

#define AUDIO_OUTPUT_BLOCK_FRAMES 2048u   // max frames per submitted block

enum Backend {
    AUDIO_NONE = 0,
    AUDIO_AC97,
    AUDIO_HDA
};

// Probe and initialize the first supported sound card.
// Returns the backend kind (AUDIO_NONE if no card is present).
int  audio_probe(void);

// Human-readable backend name ("Intel AC'97" / "Intel HD Audio" / "None").
const char* audio_backend_name(void);

// True once a card has been found and initialized.
bool audio_available(void);

// Start the output engine at a given rate (Hz) and channel count.
bool audio_output_start(uint32_t rate, uint8_t channels);

// Stop output and release the DMA engine (keeps the probe state).
void audio_output_stop(void);

// Submit one 16-bit stereo PCM block (<= AUDIO_OUTPUT_BLOCK_FRAMES frames).
// Returns false if the engine is busy with the previous block.
bool audio_output_submit(const int16_t* pcm, uint32_t frames);

// Poll for completion; returns frames played since the last call.
uint32_t audio_output_pump(void);

// Total frames played since output_start.
uint32_t audio_output_played_frames(void);

// True while a block is still in flight.
bool audio_output_busy(void);

// Master volume, 0..100.
void audio_output_volume(int percent);

} // namespace audio
} // namespace drivers
} // namespace nefu
