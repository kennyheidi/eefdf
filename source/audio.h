#ifndef AUDIO_H
#define AUDIO_H

#include <3ds.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- basic audio configuration ---

#define AUDIO_CHANNEL        0
#define AUDIO_SAMPLE_RATE    44100

// One buffer holds ~0.5s of stereo s16.
// With 3 buffers, you have ~1.5s of queued audio.
#define AUDIO_BUFFER_FRAMES  (AUDIO_SAMPLE_RATE / 2)
#define AUDIO_NUM_BUFFERS    3

// Scratch buffer for decoding before resampling.
// We decode a bit more than we need to handle pitch.
#define AUDIO_DECODE_FRAMES  (AUDIO_BUFFER_FRAMES * 2)

// --- enums ---

typedef enum {
    AUDIO_STATUS_STOPPED = 0,
    AUDIO_STATUS_PLAYING,
    AUDIO_STATUS_PAUSED
} AudioStatus;

typedef enum {
    AUDIO_FORMAT_NONE = 0,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_FLAC,
    AUDIO_FORMAT_WAV,
    AUDIO_FORMAT_OGG
} AudioFormat;

// --- main state ---

typedef struct {
    // playback state
    AudioStatus status;
    AudioFormat format;
    bool        ndsp_ok;

    // file info
    char current_file[512];
    char current_title[256];

    // user‑controlled parameters
    float pitch_semitones;   // -12 .. +12
    float speed;             // 0.25 .. 4.0
    float volume;            // 0.0 .. 1.0

    // cached values for hot path
    float pitch_ratio;       // 2^(pitch/12)
    float playback_rate;     // AUDIO_SAMPLE_RATE * speed
    s32   volume_q15;        // volume in Q0.15 fixed‑point

    // timing
    double duration_sec;
    double position_sec;

    // decoder handle
    void* decoder;

    // ndsp buffers
    ndspWaveBuf wave_buf[AUDIO_NUM_BUFFERS];
    s16*        pcm_buf[AUDIO_NUM_BUFFERS];
    int         active_buf;

    // decode scratch buffer (interleaved stereo s16)
    s16* decode_buf;
    int  decode_buf_frames;

} AudioState;

// --- public API ---

void audio_init(AudioState* a);
void audio_exit(AudioState* a);

bool audio_open(AudioState* a, const char* path);
void audio_close(AudioState* a);

void audio_play(AudioState* a);
void audio_stop(AudioState* a);
void audio_pause(AudioState* a);
void audio_resume(AudioState* a);

// call once per frame
void audio_update(AudioState* a);

// controls
void audio_adjust_pitch(AudioState* a, float semitones);
void audio_adjust_speed(AudioState* a, float delta);
void audio_reset_fx(AudioState* a);
void audio_set_volume(AudioState* a, float volume);

// queries
bool   audio_is_playing(const AudioState* a);
double audio_get_position(const AudioState* a);
double audio_get_duration(const AudioState* a);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_H
