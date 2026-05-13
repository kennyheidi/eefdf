/*
 * ui.c  —  Top & bottom screen rendering
 *
 * Top screen  (400x240):  Now Playing, waveform, pitch/speed meters
 * Bottom screen (320x240): File browser
 */

#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TEXT_BUF_GLYPHS 2048

#define COL32(r,g,b,a) C2D_Color32(r,g,b,a)
#define SCREEN_W_TOP   400
#define SCREEN_W_BOT   320
#define SCREEN_H       240
#define ROW_H           18

static void draw_rect(float x, float y, float w, float h, u32 col) {
    C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
}

/* draw_text — uses the pre-allocated shared_buf from UIState. */
static void draw_text(UIState* ui, const char* str,
                      float x, float y, float sz, u32 col) {
    C2D_Text text;
    C2D_TextParse(&text, ui->shared_buf, str);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, sz, sz, col);
}

static void draw_bar(float x, float y, float w, float h,
                     float value, float min_v, float max_v,
                     u32 bg_col, u32 fill_col) {
    draw_rect(x, y, w, h, bg_col);
    float frac = (value - min_v) / (max_v - min_v);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    draw_rect(x, y, w * frac, h, fill_col);
}

/* ------------------------------------------------------------------ */
/*  Init / fini                                                       */
/* ------------------------------------------------------------------ */

void ui_init(UIState* ui, AudioState* audio, FileBrowser* fb, void* unused) {
    (void)unused;

    memset(ui, 0, sizeof(*ui));
    ui->audio = audio;
    ui->fb    = fb;

    /* Allocate the shared text buffer ONCE — freed in ui_fini() */
    ui->shared_buf = C2D_TextBufNew(TEXT_BUF_GLYPHS);

    ui->col_bg     = COL32(0x12, 0x12, 0x1E, 0xFF);
    ui->col_accent = COL32(0x7C, 0x3A, 0xFF, 0xFF);
    ui->col_text   = COL32(0xEE, 0xEE, 0xFF, 0xFF);
    ui->col_dim    = COL32(0x88, 0x88, 0xAA, 0xFF);
    ui->col_sel    = COL32(0x2A, 0x1A, 0x55, 0xFF);
    ui->col_dir    = COL32(0x7C, 0xD0, 0xFF, 0xFF);
    ui->col_bar    = COL32(0x2A, 0x2A, 0x44, 0xFF);
}

void ui_fini(UIState* ui) {
    if (ui->shared_buf) {
        C2D_TextBufDelete(ui->shared_buf);
        ui->shared_buf = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Top screen                                                        */
/* ------------------------------------------------------------------ */

void ui_draw_top(UIState* ui, C3D_RenderTarget* target) {
    (void)target;
    AudioState* a = ui->audio;

    C2D_TextBufClear(ui->shared_buf);

    /* Header bar */
    draw_rect(0, 0, SCREEN_W_TOP, 24, ui->col_accent);
    draw_text(ui, "3DS Audio Player", 8, 4, 0.55f, COL32(0xFF,0xFF,0xFF,0xFF));

    /* Status badge */
    const char* status_label = "STOPPED";
    u32         status_col   = ui->col_dim;
    if (a->status == AUDIO_STATUS_PLAYING) {
        status_label = " PLAYING";
        status_col   = COL32(0x44,0xFF,0x88,0xFF);
    } else if (a->status == AUDIO_STATUS_PAUSED) {
        status_label = "  PAUSED";
        status_col   = COL32(0xFF,0xCC,0x44,0xFF);
    }
    draw_text(ui, status_label, 310, 4, 0.50f, status_col);

    /* Track title */
    char title_str[300];
    if (a->status != AUDIO_STATUS_STOPPED && a->current_title[0])
        snprintf(title_str, sizeof(title_str), "%s", a->current_title);
    else
        snprintf(title_str, sizeof(title_str), "No track loaded");
    draw_text(ui, title_str, 8, 30, 0.52f, ui->col_text);

    /* Progress bar */
    double pos = audio_get_position(a);
    double dur = audio_get_duration(a);
    float prog = (dur > 0.0) ? (float)(pos / dur) : 0.0f;
    if (prog < 0.0f) prog = 0.0f;
    if (prog > 1.0f) prog = 1.0f;

    draw_rect(8, 52, SCREEN_W_TOP - 16, 6, ui->col_bar);
    draw_rect(8, 52, (SCREEN_W_TOP - 16) * prog, 6, ui->col_accent);

    int pos_s = (int)pos;
    int dur_s = (int)dur;
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%d:%02d / %d:%02d",
             pos_s/60, pos_s%60, dur_s/60, dur_s%60);
    draw_text(ui, time_str, 8, 62, 0.42f, ui->col_dim);

    /* Waveform visualiser (assuming ui->wave[] and WAVE_BARS defined in ui.h) */
    audio_get_waveform(a, ui->wave, WAVE_BARS);
    float wy = 100.0f;
    float wh = 36.0f;
    draw_rect(0, wy - wh, SCREEN_W_TOP, wh * 2.0f, COL32(0x1A,0x1A,0x30,0xFF));
    for (int i = 0; i < WAVE_BARS; i++) {
        float h = fabsf(ui->wave[i]) * wh;
        if (h < 1.0f) h = 1.0f;
        u32 wc = (a->status == AUDIO_STATUS_PLAYING)
               ? COL32(0x7C, 0x3A, 0xFF, 0xCC)
               : COL32(0x44, 0x44, 0x66, 0x88);
        draw_rect(i * 4.0f, wy - h, 3.0f, h * 2.0f, wc);
    }

    /* Pitch control (assuming PITCH_MIN/MAX and a->pitch_semitones) */
    draw_text(ui, "PITCH", 8, 148, 0.45f, ui->col_dim);
    draw_bar(50, 150, 120, 10, a->pitch_semitones, -12.0f, 12.0f,
             ui->col_bar, COL32(0xFF, 0x77, 0x44, 0xFF));
    char pitch_str[16];
    snprintf(pitch_str, sizeof(pitch_str), "%+.0f st", a->pitch_semitones);
    draw_text(ui, pitch_str, 178, 148, 0.45f, COL32(0xFF,0x99,0x66,0xFF));
    draw_text(ui, "L / R buttons", 8, 162, 0.38f, ui->col_dim);

    /* Speed control (using a->speed) */
    draw_text(ui, "SPEED", 220, 148, 0.45f, ui->col_dim);
    draw_bar(262, 150, 120, 10, a->speed, 0.25f, 4.0f,
             ui->col_bar, COL32(0x44, 0xCC, 0xFF, 0xFF));
    char speed_str[16];
    snprintf(speed_str, sizeof(speed_str), "%.2fx", a->speed);
    draw_text(ui, speed_str, 388, 148, 0.45f, COL32(0x88,0xEE,0xFF,0xFF));
    draw_text(ui, "Left / Right", 220, 162, 0.38f, ui->col_dim);

    /* Controls reminder */
    draw_rect(0, 192, SCREEN_W_TOP, 1, ui->col_bar);
    draw_text(ui, "A=Play  B=Back  Sel=Pause  Sta=Stop  X=Reset",
              6, 197, 0.40f, ui->col_dim);
    draw_text(ui, "L/R=Pitch  Left/Right=Speed",
              6, 213, 0.40f, ui->col_dim);
}

/* ------------------------------------------------------------------ */
/*  Bottom screen — File Browser                                      */
/* ------------------------------------------------------------------ */

void ui_draw_bottom(UIState* ui, C3D_RenderTarget* target) {
    (void)target;
    FileBrowser* fb = ui->fb;

    C2D_TextBufClear(ui->shared_buf);

    /* Header */
    draw_rect(0, 0, SCREEN_W_BOT, 18, COL32(0x22, 0x22, 0x3A, 0xFF));
    const char* cwd_disp = fb->cwd;
    if (strlen(cwd_disp) > 38) cwd_disp = fb->cwd + strlen(fb->cwd) - 38;
    char cwd_buf[64];
    snprintf(cwd_buf, sizeof(cwd_buf), "%s", cwd_disp);
    draw_text(ui, cwd_buf, 4, 2, 0.40f, ui->col_dim);

    if (fb->count == 0) {
        draw_text(ui, "No audio files found.", 8, 40, 0.48f, ui->col_dim);
        draw_text(ui, "Put MP3/OGG/FLAC/WAV in", 8, 58, 0.42f, ui->col_dim);
        draw_text(ui, "/music on your SD card.", 8, 72, 0.42f, ui->col_dim);
        return;
    }

    /* File list */
    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = fb->scroll + i;
        if (idx >= fb->count) break;

        float ry = 20 + i * ROW_H;
        Browser
