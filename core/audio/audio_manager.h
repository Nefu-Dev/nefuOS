// nefuOS Audio System - Full Implementation (with real sound-card output)
#pragma once

#include "../klib/klib.h"
#include "../platform.h"          // kalloc/kfree (kernel arena)
#include "../drivers/audio.h"     // real PCI sound-card interface
#include <math.h>                 // sinf (tone generator)
#include <stdlib.h>               // rand (noise generator)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace nefu {
namespace audio {

// Audio formats
enum AudioFormat {
    AUDIO_FORMAT_PCM_S8,
    AUDIO_FORMAT_PCM_U8,
    AUDIO_FORMAT_PCM_S16_LE,
    AUDIO_FORMAT_PCM_S16_BE,
    AUDIO_FORMAT_PCM_U16_LE,
    AUDIO_FORMAT_PCM_U16_BE,
    AUDIO_FORMAT_PCM_S24_LE,
    AUDIO_FORMAT_PCM_S32_LE,
    AUDIO_FORMAT_FLOAT32_LE
};

// Audio stream
struct AudioStream {
    int id;
    AudioFormat format;
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t buffer_size;
    uint32_t position;
    bool playing;
    bool paused;
    bool looping;
    float volume;
    float pan;
    void* buffer;
    uint32_t buffer_len;
};

// Audio device
struct AudioDevice {
    char name[32];
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t buffer_size;
    bool playing;
    float volume;
    float mute;
};

// Audio mixer (stream mixing + real hardware output)
class AudioMixer {
private:
    AudioStream streams[16];
    int stream_count;
    AudioDevice output_device;
    bool hw_inited;      // real sound card probed + started
    bool hw_ok;          // hardware output working
    char hw_name[48];    // backend name ("Intel AC'97" / "Intel HD Audio")

public:
    // public accessors for engine (WIP: hw_ok/hw_name are private fields) 
    bool hw_ok_state() const { return hw_ok; } 
    const char* hw_name_state() const { return hw_name; } 

    AudioMixer() : stream_count(0) {
        memset(&output_device, 0, sizeof(output_device));
        strcpy(output_device.name, "Default Audio Device");
        output_device.sample_rate = 44100;
        output_device.channels = 2;
        output_device.buffer_size = 4096;
        output_device.volume = 1.0f;
        output_device.mute = 0.0f;
        hw_inited = false;
        hw_ok = false;
        hw_name[0] = 0;
    }

    // Initialize audio: probe a real PCI sound card (AC'97 / HD Audio) and
    // start the output engine.  Falls back to a software-only mixer when no
    // card is present.
    bool init() {
        if (hw_inited) return hw_ok;
        hw_inited = true;
        int b = drivers::audio::audio_probe();
        if (b == drivers::audio::AUDIO_NONE) {
            hw_ok = false;
            return false;
        }
        strncpy(hw_name, drivers::audio::audio_backend_name(), sizeof(hw_name) - 1);
        hw_name[sizeof(hw_name) - 1] = 0;
        output_device.sample_rate = 44100;
        output_device.channels = 2;
        if (drivers::audio::audio_output_start(44100, 2)) {
            hw_ok = true;
            drivers::audio::audio_output_volume((int)(output_device.volume * 100.0f));
        } else {
            hw_ok = false;
        }
        return hw_ok;
    }

    // Name of the detected sound card ("" when none).
    const char* hardware_name() const { return hw_name; }

    // True when a real sound card is driving the output.
    bool hardware_active() const { return hw_ok; }

    // Pump the hardware: complete the previous block, mix 512 frames and
    // submit them to the card.  Call this from the periodic tick loop.
    // Returns frames actually handed to the hardware.
    uint32_t hw_pump() {
        if (!hw_ok) return 0;
        uint32_t played = drivers::audio::audio_output_pump();
        if (!drivers::audio::audio_output_busy()) {
            int16_t buf[512 * 2];
            memset(buf, 0, sizeof(buf));
            mix_audio(buf, 512);
            if (drivers::audio::audio_output_submit(buf, 512)) {
                return 512;
            }
        }
        (void)played;
        return 0;
    }

    // Shutdown audio
    void shutdown() {
        if (hw_ok) drivers::audio::audio_output_stop();
        for (int i = 0; i < stream_count; i++) {
            stop_stream(i);
            if (streams[i].buffer) kfree(streams[i].buffer);
        }
        stream_count = 0;
        hw_ok = false;
    }

    // Create stream
    int create_stream(AudioFormat format, uint32_t sample_rate, uint8_t channels, uint32_t buffer_size) {
        if (stream_count >= 16) return -1;

        AudioStream& stream = streams[stream_count++];
        stream.id = stream_count - 1;
        stream.format = format;
        stream.sample_rate = sample_rate;
        stream.channels = channels;
        stream.buffer_size = buffer_size;
        stream.position = 0;
        stream.playing = false;
        stream.paused = false;
        stream.looping = false;
        stream.volume = 1.0f;
        stream.pan = 0.0f;
        stream.buffer = 0;
        stream.buffer_len = 0;

        return stream.id;
    }

    // Destroy stream
    bool destroy_stream(int id) {
        if (id < 0 || id >= stream_count) return false;

        stop_stream(id);
        if (streams[id].buffer) kfree(streams[id].buffer);
        streams[id].buffer = 0;

        return true;
    }

    // Play stream
    bool play_stream(int id) {
        if (id < 0 || id >= stream_count) return false;

        streams[id].playing = true;
        streams[id].paused = false;
        return true;
    }

    // Pause stream
    bool pause_stream(int id) {
        if (id < 0 || id >= stream_count) return false;

        streams[id].paused = true;
        return true;
    }

    // Stop stream
    bool stop_stream(int id) {
        if (id < 0 || id >= stream_count) return false;

        streams[id].playing = false;
        streams[id].paused = false;
        streams[id].position = 0;
        return true;
    }

    // Set stream volume
    bool set_stream_volume(int id, float volume) {
        if (id < 0 || id >= stream_count) return false;

        streams[id].volume = volume;
        return true;
    }

    // Set stream pan
    bool set_stream_pan(int id, float pan) {
        if (id < 0 || id >= stream_count) return false;

        streams[id].pan = pan;
        return true;
    }

    // Set stream looping
    bool set_stream_looping(int id, bool loop) {
        if (id < 0 || id >= stream_count) return false;

        streams[id].looping = loop;
        return true;
    }

    // Set stream position
    bool set_stream_position(int id, uint32_t position) {
        if (id < 0 || id >= stream_count) return false;

        streams[id].position = position;
        return true;
    }

    // Get stream position
    uint32_t get_stream_position(int id) {
        if (id < 0 || id >= stream_count) return 0;
        return streams[id].position;
    }

    // Load audio data
    bool load_stream_data(int id, const void* data, uint32_t len) {
        if (id < 0 || id >= stream_count) return false;

        if (streams[id].buffer) kfree(streams[id].buffer);
        streams[id].buffer = kalloc(len);
        memcpy(streams[id].buffer, data, len);
        streams[id].buffer_len = len;
        streams[id].position = 0;

        return true;
    }

    // Set master volume (0..1); syncs the real card volume when present.
    void set_master_volume(float volume) {
        if (volume < 0.0f) volume = 0.0f;
        if (volume > 1.0f) volume = 1.0f;
        output_device.volume = volume;
        if (hw_ok) drivers::audio::audio_output_volume((int)(volume * 100.0f));
    }

    // Get master volume
    float get_master_volume() {
        return output_device.volume;
    }

    // Mute audio
    void mute(bool m) {
        output_device.mute = m ? 1.0f : 0.0f;
    }

    // Check if muted
    bool is_muted() {
        return output_device.mute > 0.5f;
    }

    // Mix audio buffer
    void mix_audio(int16_t* output, uint32_t frames) {
        // Clear output buffer
        memset(output, 0, frames * 2 * sizeof(int16_t));

        // Mix all active streams
        for (int i = 0; i < stream_count; i++) {
            AudioStream& stream = streams[i];
            if (!stream.playing || stream.paused || !stream.buffer) continue;

            int16_t* src = (int16_t*)stream.buffer;
            uint32_t src_frames = stream.buffer_len / (stream.channels * sizeof(int16_t));

            for (uint32_t f = 0; f < frames; f++) {
                uint32_t src_frame = stream.position + f;

                if (src_frame >= src_frames) {
                    if (stream.looping) {
                        src_frame = 0;
                    } else {
                        stream.playing = false;
                        break;
                    }
                }

                // Mix left channel
                int16_t left = src[src_frame * stream.channels];
                int32_t mixed_left = (int32_t)left * (int32_t)(stream.volume * 32768.0f);
                output[f * 2] += mixed_left / 32768;

                // Mix right channel
                if (stream.channels >= 2) {
                    int16_t right = src[src_frame * stream.channels + 1];
                    int32_t mixed_right = (int32_t)right * (int32_t)(stream.volume * 32768.0f);
                    output[f * 2 + 1] += mixed_right / 32768;
                }
            }

            stream.position += frames;
            if (stream.position >= src_frames && !stream.looping) {
                stream.playing = false;
            }
        }

        // Apply master volume
        float master = output_device.volume * (1.0f - output_device.mute);
        for (uint32_t i = 0; i < frames * 2; i++) {
            int32_t sample = (int32_t)output[i] * (int32_t)(master * 32768.0f);
            output[i] = (int16_t)(sample / 32768);
        }
    }

    // Play a tone
    void play_tone(float frequency, float duration, float volume) {
        int id = create_stream(AUDIO_FORMAT_PCM_S16_LE, 44100, 1, 4410);
        if (id < 0) return;

        uint32_t samples = (uint32_t)(44100 * duration);
        int16_t* buf = (int16_t*)kalloc(samples * sizeof(int16_t));

        for (uint32_t i = 0; i < samples; i++) {
            float t = (float)i / 44100.0f;
            buf[i] = (int16_t)(sinf(2 * M_PI * frequency * t) * volume * 32767.0f);
        }

        load_stream_data(id, buf, samples * sizeof(int16_t));
        play_stream(id);

        kfree(buf);
    }

    // Play noise
    void play_noise(float duration, float volume) {
        int id = create_stream(AUDIO_FORMAT_PCM_S16_LE, 44100, 1, 4410);
        if (id < 0) return;

        uint32_t samples = (uint32_t)(44100 * duration);
        int16_t* buf = (int16_t*)kalloc(samples * sizeof(int16_t));

        for (uint32_t i = 0; i < samples; i++) {
            buf[i] = (int16_t)((rand() % 2000 - 1000) * volume);
        }

        load_stream_data(id, buf, samples * sizeof(int16_t));
        play_stream(id);

        kfree(buf);
    }
};

// Global audio mixer (C++17 inline variable: one definition across all TUs)
inline AudioMixer g_audio;

} // namespace audio
} // namespace nefu
