/*
 * audio.c  —  decoding + pitch/speed via simple resampling
 *
 * Pitch shift:  pitch_ratio = 2^(semitones/12)
 *               We resample decoded PCM at (rate * pitch_ratio) then play at
 *               the original rate, which shifts pitch without changing speed.
 *
 * Speed change: We play back the audio at (sample_rate * speed), which changes
 *               both the tempo and slightly the pitch.
 *
 * Combined:     First pitch-shift resample, then speed resample — gives
 *               independent pitch and speed control.
 *
 * Libraries used:
 *   dr_mp3.h  (single-header MP3 decoder)
 *   dr_flac.h (single-header FLAC decoder)
 *   stb_vorbis.h (single-header OGG decoder)
 *   dr_wav.h  (single-header WAV decoder)
 */

#include "audio.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// Single-header decoders — define implementation in exactly one .c file
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

// stb_vorbis: included directly here (audio.c is its one translation unit).
// stb_vorbis.c is excluded from the Makefile wildcard to prevent double-compilation.
#include "stb_vorbis.c"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static AudioFormat detect_format(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return FMT_UNKNOWN;
    if (strcasecmp(ext, ".mp3")  == 0) return FMT_MP3;
    if (strcasecmp(ext, ".ogg")  == 0) return FMT_OGG;
    if (strcasecmp(ext, ".flac") == 0) return FMT_FLAC;
    if (strcasecmp(ext, ".wav")  == 0) return FMT_WAV;
    return FMT_UNKNOWN;
}

static void extract_title(const char* path, char* title, int maxlen) {
    const char* slash = strrchr(path, '/');
    const char* src   = slash ? slash + 1 : path;
    strncpy(title, src, maxlen - 1);
    title[maxlen - 1] = '\0';
    // Strip extension
    char* dot = strrchr(title, '.');
    if (dot) *dot = '\0';
}

/*
 * Linear resample src (src_len samples) to dst at ratio dst_rate/src_rate.
 * Both are interleaved stereo s16.
 * Returns number of output samples written (stereo pairs).
 */
static int resample_stereo(const s16* src, int src_frames,
                            s16* dst, int dst_max_frames,
                            double ratio) {
    int out = 0;
    for (int i = 0; i < dst_max_frames; i++) {
        double src_pos = i / ratio;
        int    idx0    = (int)src_pos;
        int    idx1    = idx0 + 1;
        double frac    = src_pos - idx0;
        if (idx1 >= src_frames) break;

        for (int ch = 0; ch < 2; ch++) {
            double s = src[idx0 * 2 + ch] * (1.0 - frac)
                     + src[idx1 * 2 + ch] * frac;
            dst[out * 2 + ch] = (s16)fmax(-32768, fmin(32767, s));
        }
        out++;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void audio_init(AudioState* a) {
    memset(a, 0, sizeof(*a));
    a->status = AUDIO_STOPPED;
    a->volume = 1.0f;
    a->pitch  = 0.0f;
    a->speed  = 1.0f;

    // Allocate PCM double-buffers in linear memory (required by NDSP)
    for (int i = 0; i < 2; i++) {
        a->pcm_buf[i] = (s16*)linearAlloc(BUFFER_SIZE * sizeof(s16));
        memset(a->pcm_buf[i], 0, BUFFER_SIZE * sizeof(s16));
    }
    a->process_buf_size = BUFFER_SIZE * 4; // extra room for resampling
    a->process_buf      = (s16*)linearAlloc(a->process_buf_size * sizeof(s16));

    // Configure NDSP channel
    ndspChnReset(AUDIO_CHANNEL);
    ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(AUDIO_CHANNEL, SAMPLE_RATE);
    ndspChnSetFormat(AUDIO_CHANNEL, NDSP_FORMAT_STEREO_PCM16);

    float mix[12] = { 1.0f, 1.0f };
    ndspChnSetMix(AUDIO_CHANNEL, mix);
}

void audio_shutdown(AudioState* a) {
    audio_stop(a);
    for (int i = 0; i < 2; i++) {
        if (a->pcm_buf[i]) { linearFree(a->pcm_buf[i]); a->pcm_buf[i] = NULL; }
    }
    if (a->process_buf) { linearFree(a->process_buf); a->process_buf = NULL; }
}

void audio_stop(AudioState* a) {
    if (a->status == AUDIO_STOPPED) return;
    ndspChnReset(AUDIO_CHANNEL);
    a->status   = AUDIO_STOPPED;
    a->position = 0;

    // Free decoder
    if (a->decoder) {
        switch (a->format) {
            case FMT_MP3:  drmp3_uninit((drmp3*)a->decoder);       free(a->decoder); break;
            case FMT_FLAC: drflac_close((drflac*)a->decoder);      break;
            case FMT_WAV:  drwav_uninit((drwav*)a->decoder);       free(a->decoder); break;
            case FMT_OGG:  stb_vorbis_close((stb_vorbis*)a->decoder); break;
            default: break;
        }
        a->decoder = NULL;
    }
}

void audio_play(AudioState* a, const char* path) {
    audio_stop(a);

    a->format = detect_format(path);
    if (a->format == FMT_UNKNOWN) return;

    strncpy(a->current_file, path, sizeof(a->current_file) - 1);
    extract_title(path, a->current_title, sizeof(a->current_title));

    bool ok = false;
    switch (a->format) {
        case FMT_MP3: {
            drmp3* dec = calloc(1, sizeof(drmp3));
            ok = drmp3_init_file(dec, path, NULL);
            if (ok) {
                a->decoder  = dec;
                a->duration = (double)drmp3_get_pcm_frame_count(dec) / SAMPLE_RATE;
            } else free(dec);
            break;
        }
        case FMT_FLAC: {
            drflac* dec = drflac_open_file(path, NULL);
            ok = (dec != NULL);
            if (ok) {
                a->decoder  = dec;
                a->duration = (double)dec->totalPCMFrameCount / dec->sampleRate;
            }
            break;
        }
        case FMT_WAV: {
            drwav* dec = calloc(1, sizeof(drwav));
            ok = drwav_init_file(dec, path, NULL);
            if (ok) {
                a->decoder  = dec;
                a->duration = (double)dec->totalPCMFrameCount / dec->sampleRate;
            } else free(dec);
            break;
        }
        case FMT_OGG: {
            int error = 0;
            stb_vorbis* dec = stb_vorbis_open_filename(path, &error, NULL);
            ok = (dec != NULL && error == 0);
            if (ok) {
                stb_vorbis_info info = stb_vorbis_get_info(dec);
                a->decoder  = dec;
                a->duration = stb_vorbis_stream_length_in_seconds(dec);
                (void)info;
            }
            break;
        }
        default: break;
    }

    if (!ok) { a->status = AUDIO_STOPPED; return; }

    // Configure NDSP for this file
    ndspChnReset(AUDIO_CHANNEL);
    ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(AUDIO_CHANNEL, (float)(SAMPLE_RATE * a->speed));
    ndspChnSetFormat(AUDIO_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = { a->volume, a->volume };
    ndspChnSetMix(AUDIO_CHANNEL, mix);

    a->status     = AUDIO_PLAYING;
    a->active_buf = 0;
    a->position   = 0;

    // Prime both wave buffers
    for (int i = 0; i < 2; i++) {
        memset(&a->wave_buf[i], 0, sizeof(ndspWaveBuf));
        a->wave_buf[i].data_vaddr = a->pcm_buf[i];
        a->wave_buf[i].status     = NDSP_WBUF_FREE;
    }
    audio_update(a); // fill first buffer
}

void audio_toggle_pause(AudioState* a) {
    if (a->status == AUDIO_PLAYING) {
        ndspChnSetPaused(AUDIO_CHANNEL, true);
        a->status = AUDIO_PAUSED;
    } else if (a->status == AUDIO_PAUSED) {
        ndspChnSetPaused(AUDIO_CHANNEL, false);
        a->status = AUDIO_PLAYING;
    }
}

/*
 * Decode one chunk, apply pitch+speed, submit to NDSP.
 * Called every frame from main loop.
 */
void audio_update(AudioState* a) {
    if (a->status != AUDIO_PLAYING) return;
    if (!a->decoder) return;

    // Check if the active wave buffer has been consumed
    ndspWaveBuf* wb = &a->wave_buf[a->active_buf];
    if (wb->status != NDSP_WBUF_FREE && wb->status != NDSP_WBUF_DONE) return;

    // Frames to decode at original rate
    int frames_needed = BUFFER_SIZE / 2; // stereo s16 → /2 for frame count

    // Pitch ratio: 2^(semitones/12)
    double pitch_ratio = pow(2.0, a->pitch / 12.0);
    // To achieve pitch shift without speed change, decode extra frames
    int decode_frames = (int)(frames_needed / pitch_ratio) + 4;
    if (decode_frames * 2 > a->process_buf_size)
        decode_frames = a->process_buf_size / 2;

    s16* raw = a->process_buf;
    int  decoded = 0;

    switch (a->format) {
        case FMT_MP3:
            decoded = (int)drmp3_read_pcm_frames_s16((drmp3*)a->decoder,
                          decode_frames, raw);
            break;
        case FMT_FLAC:
            decoded = (int)drflac_read_pcm_frames_s16((drflac*)a->decoder,
                          decode_frames, raw);
            break;
        case FMT_WAV:
            decoded = (int)drwav_read_pcm_frames_s16((drwav*)a->decoder,
                          decode_frames, raw);
            break;
        case FMT_OGG:
            decoded = stb_vorbis_get_samples_short_interleaved(
                          (stb_vorbis*)a->decoder, 2, raw, decode_frames * 2);
            break;
        default: break;
    }

    if (decoded <= 0) {
        // End of file — stop
        a->status   = AUDIO_STOPPED;
        a->position = a->duration;
        return;
    }

    // Apply pitch resample (resample decoded frames to frames_needed)
    s16* dst = a->pcm_buf[a->active_buf];
    int out_frames = resample_stereo(raw, decoded, dst, frames_needed, pitch_ratio);

    // Update position counter
    a->position += (double)decoded / SAMPLE_RATE;

    // Set speed via NDSP playback rate
    ndspChnSetRate(AUDIO_CHANNEL, (float)(SAMPLE_RATE * a->speed));

    // Submit buffer
    wb->data_vaddr = dst;
    wb->nsamples   = out_frames * 2; // samples = frames * channels
    wb->status     = NDSP_WBUF_FREE;
    DSP_FlushDataCache(dst, out_frames * 2 * sizeof(s16));
    ndspChnWaveBufAdd(AUDIO_CHANNEL, wb);

    // Flip buffer
    a->active_buf ^= 1;
}

void audio_adjust_pitch(AudioState* a, float semitones) {
    a->pitch += semitones;
    if (a->pitch < PITCH_MIN) a->pitch = PITCH_MIN;
    if (a->pitch > PITCH_MAX) a->pitch = PITCH_MAX;
}

void audio_adjust_speed(AudioState* a, float delta) {
    a->speed += delta;
    if (a->speed < SPEED_MIN) a->speed = SPEED_MIN;
    if (a->speed > SPEED_MAX) a->speed = SPEED_MAX;
    if (a->status == AUDIO_PLAYING)
        ndspChnSetRate(AUDIO_CHANNEL, (float)(SAMPLE_RATE * a->speed));
}

void audio_reset_fx(AudioState* a) {
    a->pitch = 0.0f;
    a->speed = 1.0f;
    if (a->status == AUDIO_PLAYING)
        ndspChnSetRate(AUDIO_CHANNEL, SAMPLE_RATE);
}

void audio_set_volume(AudioState* a, float vol) {
    a->volume = vol;
    float mix[12] = { vol, vol };
    ndspChnSetMix(AUDIO_CHANNEL, mix);
}

float audio_progress(const AudioState* a) {
    if (a->duration <= 0) return 0.0f;
    float p = (float)(a->position / a->duration);
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

void audio_get_waveform(const AudioState* a, float* buf, int n) {
    if (a->status == AUDIO_STOPPED || !a->pcm_buf[a->active_buf ^ 1]) {
        memset(buf, 0, n * sizeof(float));
        return;
    }
    s16* src   = a->pcm_buf[a->active_buf ^ 1];
    int  total = BUFFER_SIZE / 2; // stereo pairs
    for (int i = 0; i < n; i++) {
        int idx = (int)((float)i / n * total) * 2; // left channel
        buf[i]  = src[idx] / 32768.0f;
    }
}
