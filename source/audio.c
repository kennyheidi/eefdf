#include "audio.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

// your decoders
#include "dr_mp3.h"
#include "dr_flac.h"
#include "dr_wav.h"
#include "stb_vorbis.c"

// --- helpers: format detection ------------------------------------------------

static AudioFormat audio_detect_format(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return AUDIO_FORMAT_NONE;
    dot++;

    if (!strcasecmp(dot, "mp3"))  return AUDIO_FORMAT_MP3;
    if (!strcasecmp(dot, "flac")) return AUDIO_FORMAT_FLAC;
    if (!strcasecmp(dot, "wav"))  return AUDIO_FORMAT_WAV;
    if (!strcasecmp(dot, "ogg"))  return AUDIO_FORMAT_OGG;

    return AUDIO_FORMAT_NONE;
}

// --- helpers: decoder open/close ---------------------------------------------

static void* audio_open_decoder(AudioState* a, const char* path) {
    switch (a->format) {
    case AUDIO_FORMAT_MP3: {
        drmp3* mp3 = (drmp3*)malloc(sizeof(drmp3));
        if (!mp3) return NULL;
        if (!drmp3_init_file(mp3, path, NULL)) {
            free(mp3);
            return NULL;
        }
        a->duration_sec = (double)mp3->totalPCMFrameCount / (double)AUDIO_SAMPLE_RATE;
        return mp3;
    }
    case AUDIO_FORMAT_FLAC: {
        drflac* flac = drflac_open_file(path, NULL);
        if (!flac) return NULL;
        a->duration_sec = (double)flac->totalPCMFrameCount / (double)AUDIO_SAMPLE_RATE;
        return flac;
    }
    case AUDIO_FORMAT_WAV: {
        drwav* wav = (drwav*)malloc(sizeof(drwav));
        if (!wav) return NULL;
        if (!drwav_init_file(wav, path, NULL)) {
            free(wav);
            return NULL;
        }
        a->duration_sec = (double)wav->totalPCMFrameCount / (double)AUDIO_SAMPLE_RATE;
        return wav;
    }
    case AUDIO_FORMAT_OGG: {
        int error = 0;
        stb_vorbis* ogg = stb_vorbis_open_filename(path, &error, NULL);
        if (!ogg) return NULL;
        stb_vorbis_info info = stb_vorbis_get_info(ogg);
        a->duration_sec = (double)stb_vorbis_stream_length_in_samples(ogg) / (double)info.sample_rate;
        return ogg;
    }
    default:
        return NULL;
    }
}

static void audio_close_decoder(AudioState* a) {
    if (!a->decoder) return;

    switch (a->format) {
    case AUDIO_FORMAT_MP3:
        drmp3_uninit((drmp3*)a->decoder);
        free(a->decoder);
        break;
    case AUDIO_FORMAT_FLAC:
        drflac_close((drflac*)a->decoder);
        break;
    case AUDIO_FORMAT_WAV:
        drwav_uninit((drwav*)a->decoder);
        free(a->decoder);
        break;
    case AUDIO_FORMAT_OGG:
        stb_vorbis_close((stb_vorbis*)a->decoder);
        break;
    default:
        break;
    }

    a->decoder = NULL;
}

// --- helpers: cached parameters ----------------------------------------------

static void audio_recalc_pitch(AudioState* a) {
    a->pitch_ratio = powf(2.0f, a->pitch_semitones / 12.0f);
}

static void audio_recalc_speed(AudioState* a) {
    a->playback_rate = (float)AUDIO_SAMPLE_RATE * a->speed;
    if (a->ndsp_ok) {
        ndspChnSetRate(AUDIO_CHANNEL, a->playback_rate);
    }
}

static void audio_recalc_volume(AudioState* a) {
    if (a->volume < 0.0f) a->volume = 0.0f;
    if (a->volume > 1.0f) a->volume = 1.0f;
    a->volume_q15 = (s32)(a->volume * 32767.0f);
}

// --- fixed‑point linear resampler (stereo) -----------------------------------
// in:  interleaved stereo, in_frames frames
// out: interleaved stereo, out_frames frames
// returns number of output frames written

static int audio_resample_linear_stereo_q16(
    const s16* in, int in_frames,
    s16* out, int out_frames,
    s32 volume_q15)
{
    if (in_frames <= 0 || out_frames <= 0)
        return 0;

    // Q16.16 position
    u32 pos = 0;
    u32 step = 0;

    if (out_frames > 1 && in_frames > 1) {
        step = ((u32)(in_frames - 1) << 16) / (u32)(out_frames - 1);
    }

    for (int i = 0; i < out_frames; i++) {
        int idx  = (int)(pos >> 16);
        int frac = (int)(pos & 0xFFFF);

        if (idx >= in_frames - 1)
            idx = in_frames - 2;

        const s16* s0 = &in[idx * 2];
        const s16* s1 = &in[(idx + 1) * 2];

        // linear interpolation per channel
        s32 l = ((s32)s0[0] * (0x10000 - frac) + (s32)s1[0] * frac) >> 16;
        s32 r = ((s32)s0[1] * (0x10000 - frac) + (s32)s1[1] * frac) >> 16;

        // apply volume (Q0.15)
        l = (l * volume_q15) >> 15;
        r = (r * volume_q15) >> 15;

        // clamp
        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;

        out[i * 2 + 0] = (s16)l;
        out[i * 2 + 1] = (s16)r;

        pos += step;
    }

    return out_frames;
}

// --- public API --------------------------------------------------------------

void audio_init(AudioState* a) {
    memset(a, 0, sizeof(*a));

    a->status          = AUDIO_STATUS_STOPPED;
    a->format          = AUDIO_FORMAT_NONE;
    a->pitch_semitones = 0.0f;
    a->speed           = 1.0f;
    a->volume          = 1.0f;
    a->duration_sec    = 0.0;
    a->position_sec    = 0.0;
    a->decode_buf      = NULL;
    a->decode_buf_frames = AUDIO_DECODE_FRAMES;

    audio_recalc_pitch(a);
    audio_recalc_speed(a);
    audio_recalc_volume(a);

    // allocate buffers
    for (int i = 0; i < AUDIO_NUM_BUFFERS; i++) {
        a->pcm_buf[i] = (s16*)linearAlloc(AUDIO_BUFFER_FRAMES * 2 * sizeof(s16));
        memset(&a->wave_buf[i], 0, sizeof(ndspWaveBuf));
    }

    a->decode_buf = (s16*)linearAlloc(AUDIO_DECODE_FRAMES * 2 * sizeof(s16));

    // init ndsp
    if (ndspInit() == 0) {
        a->ndsp_ok = true;

        ndspChnReset(AUDIO_CHANNEL);
        ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_POLYPHASE);
        ndspChnSetRate(AUDIO_CHANNEL, a->playback_rate);
        ndspChnSetFormat(AUDIO_CHANNEL, NDSP_FORMAT_STEREO_PCM16);
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    } else {
        a->ndsp_ok = false;
    }

    a->active_buf = 0;
}

void audio_exit(AudioState* a) {
    audio_stop(a);
    audio_close(a);

    if (a->decode_buf) {
        linearFree(a->decode_buf);
        a->decode_buf = NULL;
    }

    for (int i = 0; i < AUDIO_NUM_BUFFERS; i++) {
        if (a->pcm_buf[i]) {
            linearFree(a->pcm_buf[i]);
            a->pcm_buf[i] = NULL;
        }
    }

    if (a->ndsp_ok) {
        ndspExit();
        a->ndsp_ok = false;
    }
}

bool audio_open(AudioState* a, const char* path) {
    audio_stop(a);
    audio_close(a);

    a->format = audio_detect_format(path);
    if (a->format == AUDIO_FORMAT_NONE)
        return false;

    strncpy(a->current_file, path, sizeof(a->current_file) - 1);
    a->current_file[sizeof(a->current_file) - 1] = '\0';

    a->decoder = audio_open_decoder(a, path);
    if (!a->decoder) {
        a->format = AUDIO_FORMAT_NONE;
        return false;
    }

    a->position_sec = 0.0;
    return true;
}

void audio_close(AudioState* a) {
    audio_close_decoder(a);
    a->format = AUDIO_FORMAT_NONE;
    a->current_file[0] = '\0';
    a->current_title[0] = '\0';
    a->duration_sec = 0.0;
    a->position_sec = 0.0;
}

void audio_play(AudioState* a) {
    if (!a->decoder || !a->ndsp_ok)
        return;

    // reset decoder to start
    audio_close_decoder(a);
    a->decoder = audio_open_decoder(a, a->current_file);
    a->position_sec = 0.0;

    // clear and queue initial buffers
    for (int i = 0; i < AUDIO_NUM_BUFFERS; i++) {
        memset(a->pcm_buf[i], 0, AUDIO_BUFFER_FRAMES * 2 * sizeof(s16));
        memset(&a->wave_buf[i], 0, sizeof(ndspWaveBuf));

        a->wave_buf[i].data_vaddr = a->pcm_buf[i];
        a->wave_buf[i].nsamples   = AUDIO_BUFFER_FRAMES;
        a->wave_buf[i].status     = NDSP_WBUF_FREE;
    }

    a->active_buf = 0;
    a->status     = AUDIO_STATUS_PLAYING;

    audio_recalc_pitch(a);
    audio_recalc_speed(a);
    audio_recalc_volume(a);

    // fill as many buffers as possible immediately
    audio_update(a);
}

void audio_stop(AudioState* a) {
    if (!a->ndsp_ok)
        return;

    ndspChnReset(AUDIO_CHANNEL);
    for (int i = 0; i < AUDIO_NUM_BUFFERS; i++) {
        memset(&a->wave_buf[i], 0, sizeof(ndspWaveBuf));
    }

    a->status = AUDIO_STATUS_STOPPED;
}

void audio_pause(AudioState* a) {
    if (a->status == AUDIO_STATUS_PLAYING) {
        a->status = AUDIO_STATUS_PAUSED;
        if (a->ndsp_ok)
            ndspChnSetPaused(AUDIO_CHANNEL, true);
    }
}

void audio_resume(AudioState* a) {
    if (a->status == AUDIO_STATUS_PAUSED) {
        a->status = AUDIO_STATUS_PLAYING;
        if (a->ndsp_ok)
            ndspChnSetPaused(AUDIO_CHANNEL, false);
    }
}

// --- decoding one chunk ------------------------------------------------------

static int audio_decode_frames(AudioState* a, int frames_to_decode, s16* dst) {
    int decoded = 0;

    switch (a->format) {
    case AUDIO_FORMAT_MP3:
        decoded = (int)drmp3_read_pcm_frames_s16((drmp3*)a->decoder,
                                                 frames_to_decode, dst);
        break;
    case AUDIO_FORMAT_FLAC:
        decoded = (int)drflac_read_pcm_frames_s16((drflac*)a->decoder,
                                                  frames_to_decode, dst);
        break;
    case AUDIO_FORMAT_WAV:
        decoded = (int)drwav_read_pcm_frames_s16((drwav*)a->decoder,
                                                 frames_to_decode, dst);
        break;
    case AUDIO_FORMAT_OGG:
        decoded = stb_vorbis_get_samples_short_interleaved(
            (stb_vorbis*)a->decoder, 2, dst, frames_to_decode * 2);
        break;
    default:
        decoded = 0;
        break;
    }

    return decoded;
}

// --- main update loop --------------------------------------------------------
// Call once per frame from your main loop.

void audio_update(AudioState* a) {
    if (a->status != AUDIO_STATUS_PLAYING)
        return;
    if (!a->decoder || !a->ndsp_ok)
        return;

    // Try to keep all buffers queued.
    for (int n = 0; n < AUDIO_NUM_BUFFERS; n++) {
        ndspWaveBuf* wb = &a->wave_buf[a->active_buf];

        if (wb->status != NDSP_WBUF_FREE && wb->status != NDSP_WBUF_DONE)
            break; // this buffer is still in use

        // how many frames we want in this NDSP buffer
        const int out_frames = AUDIO_BUFFER_FRAMES;

        // how many frames to decode to account for pitch
        float pitch_ratio = a->pitch_ratio;
        if (pitch_ratio < 0.25f) pitch_ratio = 0.25f;
        if (pitch_ratio > 4.0f)  pitch_ratio = 4.0f;

        int decode_frames = (int)((float)out_frames / pitch_ratio) + 4;
        if (decode_frames > a->decode_buf_frames)
            decode_frames = a->decode_buf_frames;

        int got = audio_decode_frames(a, decode_frames, a->decode_buf);
        if (got <= 0) {
            // end of stream
            a->status       = AUDIO_STATUS_STOPPED;
            a->position_sec = a->duration_sec;
            return;
        }

        // resample into the NDSP buffer
        s16* out = a->pcm_buf[a->active_buf];
        int  written = audio_resample_linear_stereo_q16(
            a->decode_buf, got, out, out_frames, a->volume_q15);

        // update playback position based on decoded frames
        a->position_sec += (double)got / (double)AUDIO_SAMPLE_RATE;
        if (a->position_sec > a->duration_sec)
            a->position_sec = a->duration_sec;

        wb->data_vaddr = out;
        wb->nsamples   = written; // frames per channel
        wb->status     = NDSP_WBUF_FREE;

        DSP_FlushDataCache(out, written * 2 * sizeof(s16));
        ndspChnWaveBufAdd(AUDIO_CHANNEL, wb);

        a->active_buf = (a->active_buf + 1) % AUDIO_NUM_BUFFERS;
    }
}

// --- controls / queries ------------------------------------------------------

void audio_adjust_pitch(AudioState* a, float semitones) {
    a->pitch_semitones += semitones;
    if (a->pitch_semitones < -12.0f) a->pitch_semitones = -12.0f;
    if (a->pitch_semitones >  12.0f) a->pitch_semitones =  12.0f;
    audio_recalc_pitch(a);
}

void audio_adjust_speed(AudioState* a, float delta) {
    a->speed += delta;
    if (a->speed < 0.25f) a->speed = 0.25f;
    if (a->speed > 4.0f)  a->speed = 4.0f;
    audio_recalc_speed(a);
}

void audio_reset_fx(AudioState* a) {
    a->pitch_semitones = 0.0f;
    a->speed           = 1.0f;
    audio_recalc_pitch(a);
    audio_recalc_speed(a);
}

void audio_set_volume(AudioState* a, float volume) {
    a->volume = volume;
    audio_recalc_volume(a);
}

bool audio_is_playing(const AudioState* a) {
    return a->status == AUDIO_STATUS_PLAYING;
}

double audio_get_position(const AudioState* a) {
    return a->position_sec;
}

double audio_get_duration(const AudioState* a) {
    return a->duration_sec;
}
