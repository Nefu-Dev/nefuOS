// nefuOS Music Player Application
// Play WAV, MP3, OGG files with playlist, volume control, seek
#pragma once

#include "../gfx/gfx.h"
#include "../wm.h"
#include "../vfs/vfs.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace apps {

// ============================================
// Music Format Support
// ============================================

enum MusicFormat {
    MUSIC_UNKNOWN,
    MUSIC_WAV,
    MUSIC_MP3,
    MUSIC_OGG,
    MUSIC_FLAC
};

// ============================================
// Track Info
// ============================================

struct TrackInfo {
    char title[128];
    char artist[128];
    char album[128];
    int duration;  // seconds
    int bitrate;   // kbps
    int sample_rate;
    int channels;
    MusicFormat format;
    
    TrackInfo() : duration(0), bitrate(0), sample_rate(44100), channels(2), format(MUSIC_UNKNOWN) {
        title[0] = 0;
        artist[0] = 0;
        album[0] = 0;
    }
};

// ============================================
// Playlist
// ============================================

#define MAX_PLAYLIST 100

struct Playlist {
    TrackInfo tracks[MAX_PLAYLIST];
    int count;
    int current_track;
    bool playing;
    bool paused;
    int position;  // seconds
    int volume;    // 0-100
    bool shuffle;
    bool repeat;
    
    Playlist() : count(0), current_track(-1), playing(false), paused(false), position(0), volume(80), shuffle(false), repeat(false) {}
    
    void add_track(const TrackInfo& track) {
        if (count >= MAX_PLAYLIST) return;
        tracks[count++] = track;
    }
    
    void play(int index) {
        if (index < 0 || index >= count) return;
        current_track = index;
        playing = true;
        paused = false;
        position = 0;
    }
    
    void pause() {
        paused = true;
    }
    
    void resume() {
        paused = false;
    }
    
    void stop() {
        playing = false;
        paused = false;
        position = 0;
    }
    
    void next() {
        if (shuffle) {
            current_track = (current_track + 1 + (platform_tick_ms() % (count - 1))) % count;
        } else {
            current_track = (current_track + 1) % count;
        }
        position = 0;
    }
    
    void previous() {
        current_track = (current_track - 1 + count) % count;
        position = 0;
    }
    
    void seek(int seconds) {
        position = seconds;
    }
    
    void set_volume(int vol) {
        volume = vol;
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
    }
};

// ============================================
// Music Player State
// ============================================

struct MusicPlayerState {
    Playlist playlist;
    char status_text[128];
    
    MusicPlayerState() {
        status_text[0] = 0;
        update_status();
    }
    
    void update_status() {
        if (!playlist.playing) {
            ksprintf(status_text, sizeof(status_text), "Stopped");
        } else if (playlist.paused) {
            ksprintf(status_text, sizeof(status_text), "Paused");
        } else {
            ksprintf(status_text, sizeof(status_text), "Playing");
        }
    }
};

// ============================================
// Music Player Paint
// ============================================

static void music_player_paint(Window* w) {
    MusicPlayerState* st = (MusicPlayerState*)w->userdata;
    if (!st) return;
    
    Surface& s = w->back;
    
    // Dark background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x1A1A2E);
    
    // Title
    gfx::text(s, 20, 20, "nefuOS Music Player", 0xFFFFFF, 0x1A1A2E);
    
    // Current track info
    if (st->playlist.current_track >= 0 && st->playlist.current_track < st->playlist.count) {
        TrackInfo& track = st->playlist.tracks[st->playlist.current_track];
        
        gfx::text(s, 20, 60, track.title, 0xFFFFFF, 0x1A1A2E);
        gfx::text(s, 20, 80, track.artist, 0xAAAAAA, 0x1A1A2E);
        gfx::text(s, 20, 100, track.album, 0x888888, 0x1A1A2E);
    } else {
        gfx::text(s, 20, 60, "No track selected", 0x888888, 0x1A1A2E);
    }
    
    // Progress bar
    int bar_x = 20;
    int bar_y = 140;
    int bar_w = w->content_w - 40;
    int bar_h = 8;
    
    gfx::rect(s, bar_x, bar_y, bar_w, bar_h, 0x444444);
    
    if (st->playlist.current_track >= 0) {
        TrackInfo& track = st->playlist.tracks[st->playlist.current_track];
        if (track.duration > 0) {
            int fill_w = bar_w * st->playlist.position / track.duration;
            gfx::fillrect(s, bar_x, bar_y, fill_w, bar_h, 0x00CCFF);
        }
    }
    
    // Time display
    char time[64];
    int mins = st->playlist.position / 60;
    int secs = st->playlist.position % 60;
    ksprintf(time, sizeof(time), "%d:%02d", mins, secs);
    gfx::text(s, bar_x, bar_y + 12, time, 0xAAAAAA, 0x1A1A2E);
    
    // Control buttons
    int btn_y = 180;
    int btn_w = 50;
    int btn_h = 30;
    
    // Previous
    gfx::rect(s, 20, btn_y, btn_w, btn_h, 0x444444);
    gfx::text(s, 35, btn_y + 7, "<<", 0xFFFFFF, 0x444444);
    
    // Play/Pause
    gfx::rect(s, 80, btn_y, btn_w, btn_h, 0x00CCFF);
    gfx::text(s, 95, btn_y + 7, st->playlist.paused ? ">" : "||", 0xFFFFFF, 0x00CCFF);
    
    // Stop
    gfx::rect(s, 140, btn_y, btn_w, btn_h, 0x444444);
    gfx::text(s, 155, btn_y + 7, "[]", 0xFFFFFF, 0x444444);
    
    // Next
    gfx::rect(s, 200, btn_y, btn_w, btn_h, 0x444444);
    gfx::text(s, 215, btn_y + 7, ">>", 0xFFFFFF, 0x444444);
    
    // Volume
    gfx::text(s, 20, 240, "Volume:", 0xAAAAAA, 0x1A1A2E);
    gfx::rect(s, 100, 240, 150, 8, 0x444444);
    gfx::fillrect(s, 100, 240, 150 * st->playlist.volume / 100, 8, 0x00CCFF);
    
    // Status
    gfx::text(s, 20, 280, st->status_text, 0x888888, 0x1A1A2E);
    
    // Playlist
    gfx::text(s, 20, 320, "Playlist:", 0xAAAAAA, 0x1A1A2E);
    
    for (int i = 0; i < st->playlist.count && i < 10; i++) {
        char line[128];
        ksprintf(line, sizeof(line), "%d. %s - %s", i + 1, 
                 st->playlist.tracks[i].title,
                 st->playlist.tracks[i].artist);
        gfx::text(s, 20, 340 + i * 16, line, 
                  (i == st->playlist.current_track) ? 0x00CCFF : 0xCCCCCC,
                  0x1A1A2E);
    }
}

// ============================================
// Music Player Key Handler
// ============================================

static void music_player_key(Window* w, KeyEvent* key) {
    if (!key->down) return;
    
    MusicPlayerState* st = (MusicPlayerState*)w->userdata;
    if (!st) return;
    
    if (key->ascii == ' ') {  // Space - play/pause
        if (st->playlist.playing) {
            if (st->playlist.paused) {
                st->playlist.resume();
            } else {
                st->playlist.pause();
            }
        } else if (st->playlist.count > 0) {
            st->playlist.play(0);
        }
        st->update_status();
    } else if (key->ascii == 'n' || key->ascii == 'N') {  // Next
        st->playlist.next();
        st->update_status();
    } else if (key->ascii == 'p' || key->ascii == 'P') {  // Previous
        st->playlist.previous();
        st->update_status();
    } else if (key->ascii == '+' || key->ascii == '=') {  // Volume up
        st->playlist.set_volume(st->playlist.volume + 10);
    } else if (key->ascii == '-' || key->ascii == '_') {  // Volume down
        st->playlist.set_volume(st->playlist.volume - 10);
    }
}

// ============================================
// Open Music Player
// ============================================

Window* open_music_player() {
    Window* w = new_window("Music Player", 500, 400);
    if (!w) return 0;
    
    MusicPlayerState* st = new MusicPlayerState();
    
    // Add some demo tracks
    TrackInfo track1;
    ksprintf(track1.title, 128, "NeFu Theme");
    ksprintf(track1.artist, 128, "nefuOS");
    ksprintf(track1.album, 128, "Default");
    track1.duration = 180;
    track1.format = MUSIC_WAV;
    st->playlist.add_track(track1);
    
    TrackInfo track2;
    ksprintf(track2.title, 128, "Boot Sequence");
    ksprintf(track2.artist, 128, "nefuOS");
    ksprintf(track2.album, 128, "Default");
    track2.duration = 45;
    track2.format = MUSIC_WAV;
    st->playlist.add_track(track2);
    
    w->userdata = st;
    w->on_paint = music_player_paint;
    w->on_key = music_player_key;
    
    return w;
}

} // namespace apps
} // namespace nefu
