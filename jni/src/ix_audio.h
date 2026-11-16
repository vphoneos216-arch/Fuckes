#pragma once
// ── Menu sounds ─────────────────────────────────────────────
// One AAudio output stream, opened on first use and left open. Its data
// callback mixes however many one-shot clips are playing, so a switch
// tapped twice in a row does not cut its own sound off and nothing ever
// blocks the render thread.
//
// The clips are compiled into the binary (UI/sounds/ix_sound_data.h), all
// at one format — 48 kHz, mono, signed 16-bit — so the callback only has to
// add samples together. No file is read at runtime, so there is nothing on
// disk to swap out.
//
// Every failure path is silent: if AAudio is missing or the stream will not
// open, the menu simply makes no sound.

#include <aaudio/AAudio.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "UI/sounds/ix_sound_data.h"

enum IxSound { IX_SND_WELCOME = 0, IX_SND_ON = 1, IX_SND_OFF = 2 };

namespace ix_audio_detail {

struct Clip { const int16_t* pcm; int32_t len; };

inline Clip clip_for(IxSound s) {
    switch (s) {
        case IX_SND_ON:  return { ix_snd_on,  ix_snd_on_len  };
        case IX_SND_OFF: return { ix_snd_off, ix_snd_off_len };
        default:         return { ix_snd_welcome, ix_snd_welcome_len };
    }
}

// A voice is one clip being played. `pos` is written only by the audio
// callback and read by the starter, so it is atomic; `active` is the handshake
// between the two.
struct Voice {
    std::atomic<bool>    active{false};
    std::atomic<int32_t> pos{0};
    const int16_t*       pcm = nullptr;
    int32_t              len = 0;
};

constexpr int kVoices = 6;
inline Voice            g_voices[kVoices];
inline AAudioStream*    g_stream   = nullptr;
inline std::atomic<bool> g_ready{false};
inline std::atomic<bool> g_tried{false};
inline std::atomic<bool> g_muted{false};

inline aaudio_data_callback_result_t on_audio(AAudioStream*, void*, void* audioData,
                                              int32_t numFrames) {
    int16_t* out = static_cast<int16_t*>(audioData);
    std::memset(out, 0, sizeof(int16_t) * (size_t)numFrames);

    for (int v = 0; v < kVoices; ++v) {
        Voice& vo = g_voices[v];
        if (!vo.active.load(std::memory_order_acquire)) continue;

        int32_t pos = vo.pos.load(std::memory_order_relaxed);
        const int32_t remain = vo.len - pos;
        const int32_t n = (remain < numFrames) ? remain : numFrames;

        for (int32_t i = 0; i < n; ++i) {
            // Mix in 32-bit and clamp, so several clips at once cannot wrap.
            int32_t mixed = (int32_t)out[i] + (int32_t)vo.pcm[pos + i];
            if (mixed >  32767) mixed =  32767;
            if (mixed < -32768) mixed = -32768;
            out[i] = (int16_t)mixed;
        }

        pos += n;
        if (pos >= vo.len) {
            vo.pos.store(0, std::memory_order_relaxed);
            vo.active.store(false, std::memory_order_release);
        } else {
            vo.pos.store(pos, std::memory_order_relaxed);
        }
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

inline bool ensure_stream() {
    if (g_ready.load(std::memory_order_acquire)) return true;
    // One attempt only: a device without AAudio should not be retried on
    // every single tap.
    bool expected = false;
    if (!g_tried.compare_exchange_strong(expected, true)) return false;

    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || !b) return false;

    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, IX_SND_CHANNELS);
    AAudioStreamBuilder_setSampleRate(b, IX_SND_RATE);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, on_audio, nullptr);

    AAudioStream* s = nullptr;
    const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !s) return false;

    if (AAudioStream_requestStart(s) != AAUDIO_OK) {
        AAudioStream_close(s);
        return false;
    }

    g_stream = s;
    g_ready.store(true, std::memory_order_release);
    return true;
}

} // namespace ix_audio_detail

// Start a clip. Safe to call from the render thread and safe to call again
// while the same clip is still playing — it takes the next free voice.
inline void ix_sound_play(IxSound which) {
    using namespace ix_audio_detail;
    if (g_muted.load(std::memory_order_relaxed)) return;
    if (!ensure_stream()) return;

    const Clip c = clip_for(which);
    if (!c.pcm || c.len <= 0) return;

    // Only the UI thread starts clips, so a plain check is enough here. The
    // fields are filled BEFORE `active` is published, and the callback reads
    // `active` with acquire, so it can never see a half-filled voice.
    for (int v = 0; v < kVoices; ++v) {
        if (g_voices[v].active.load(std::memory_order_acquire)) continue;
        g_voices[v].pcm = c.pcm;
        g_voices[v].len = c.len;
        g_voices[v].pos.store(0, std::memory_order_relaxed);
        g_voices[v].active.store(true, std::memory_order_release);
        return;
    }
    // every voice busy: drop this one rather than cutting another short
}

// [NEW] Mute toggle for the "Volume Off" setting. Muting does not stop a
// clip already mixing in the callback (it will finish on its own in well
// under a second); it only blocks new clips from starting, which is what
// a mute switch is expected to do and keeps on_audio() itself untouched.
inline void ix_sound_set_muted(bool muted) {
    ix_audio_detail::g_muted.store(muted, std::memory_order_relaxed);
}
inline bool ix_sound_is_muted() {
    return ix_audio_detail::g_muted.load(std::memory_order_relaxed);
}

// Plays once per process, the first time the menu is shown.
inline void ix_sound_welcome_once() {
    static std::atomic<bool> played{false};
    bool expected = false;
    if (played.compare_exchange_strong(expected, true))
        ix_sound_play(IX_SND_WELCOME);
}
