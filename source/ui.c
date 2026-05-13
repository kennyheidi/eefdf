/*
 * ui.c  —  Top & bottom screen rendering
 *
 * Key fix: C2D_TextBuf is allocated ONCE in ui_init and cleared each
 * frame with C2D_TextBufClear(). Previously we called C2D_TextBufNew/
 * Delete on every draw_text() call (hundreds per frame), which hammered
 * the heap and caused a stack-corrupting data abort.
 */

#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#define COL32(r,g,b,a) C2D_Color32(r,g,b,a)
#define SCREEN_W_TOP   400
#define SCREEN_W_BOT   320
#define SCREEN_H       240
#define ROW_H           18

// Large enough for all strings drawn in one frame
#define TEXTBUF_SIZE   4096

/* ------------------------------------------------------------------ */
/*  Strings table                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* app_title;
    const char* status_playing;
    const char* status_paused;
    const char* status_stopped;
    const char* no_track;
    const char* pitch_label;
    const char* pitch_buttons;
    const char* speed_label;
    const char* speed_buttons;
    const char* controls_line1;
    const char* controls_line2;
    const char* no_files_1;
    const char* no_files_2;
    const char* no_files_3;
    const char* lang_indicator;
} Strings;

static const Strings STR[LANG_COUNT] = {
    // LANG_EN
    {
        .app_title      = "3DS Audio Player",
        .status_playing = "PLAYING",
        .status_paused  = "PAUSED",
        .status_stopped = "STOPPED",
        .no_track       = "No track loaded",
        .pitch_label    = "PITCH",
        .pitch_buttons  = "L / R buttons",
        .speed_label    = "SPEED",
        .speed_buttons  = "Left / Right",
        .controls_line1 = "A=Play  B=Back  Sel=Pause  Sta=Stop  X=Reset",
        .controls_line2 = "L/R=Pitch  Left/Right=Speed  Y=Language",
        .no_files_1     = "No audio files found.",
        .no_files_2     = "Put MP3/OGG/FLAC/WAV in",
        .no_files_3     = "/music on your SD card.",
        .lang_indicator = "EN",
    },
    // LANG_JA (romaji)
    {
        .app_title      = "3DS Ongaku Pureyaa",
        .status_playing = "Saisei-chuu",
        .status_paused  = "Ichijiwa",
        .status_stopped = "Teishi",
        .no_track       = "Kyoku nashi",
        .pitch_label    = "Pichi",
        .pitch_buttons  = "L/R botan",
        .speed_label    = "Supido",
        .speed_buttons  = "Hidari/Migi",
        .controls_line1 = "A=Saisei  B=Modoru  Sel=Teishi  Sta=Tomeru  X=Risetto",
        .controls_line2 = "L/R=Pichi  Hidari/Migi=Supido  Y=Gengo",
        .no_files_1     = "Fairu ga mitsukaremasen.",
        .no_files_2     = "MP3/OGG/FLAC/WAV wo",
        .no_files_3     = "SDkaado /music ni irete.",
        .lang_indicator = "JA",
    },
};

/* ------------------------------------------------------------------ */
/*  Helpers — use shared textbuf, never alloc/free per draw call       */
/* ------------------------------------------------------------------ */

static UIState* g_ui = NULL; // set in ui_init for helper access

static void draw_rect(float x, float y, float w, float h, u32 col) {
    C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
}

static void draw_text(UIState* ui, const char* str,
                      float x, float y, float sz, u32 col) {
    C2D_Text text;
    C2D_TextParse(&text, ui->textbuf, str);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, sz, sz, col);
}

static void draw_textf(UIState* ui, float x, float y, float sz, u32 col,
                       const char* fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    draw_text(ui, buf, x, y, sz, col);
}

static void draw_bar(float x, float y, float w, float h,
                     float value, float min_v, float max_v,
                     u32 bg_col, u32 fill_col) {
    draw_rect(x, y, w, h, bg_col);
    float frac = (value - min_v) / (max_v - min_v);
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    draw_rect(x, y, w * frac, h, fill_col);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void ui_init(UIState* ui, AudioState* audio, FileBrowser* fb) {
    memset(ui, 0, sizeof(*ui));
    ui->audio   = audio;
    ui->fb      = fb;
    ui->lang    = LANG_EN;
    ui->textbuf = C2D_TextBufNew(TEXTBUF_SIZE);

    ui->col_bg     = COL32(0x12, 0x12, 0x1E, 0xFF);
    ui->col_accent = COL32(0x7C, 0x3A, 0xFF, 0xFF);
    ui->col_text   = COL32(0xEE, 0xEE, 0xFF, 0xFF);
    ui->col_dim    = COL32(0x88, 0x88, 0xAA, 0xFF);
    ui->col_sel    = COL32(0x2A, 0x1A, 0x55, 0xFF);
    ui->col_dir    = COL32(0x7C, 0xD0, 0xFF, 0xFF);
    ui->col_bar    = COL32(0x2A, 0x2A, 0x44, 0xFF);
}

void ui_free(UIState* ui) {
    if (ui->textbuf) {
        C2D_TextBufDelete(ui->textbuf);
        ui->textbuf = NULL;
    }
}

void ui_cycle_language(UIState* ui) {
    ui->lang = (ui->lang + 1) % LANG_COUNT;
}

/* ------------------------------------------------------------------ */
/*  Top screen                                                          */
/* ------------------------------------------------------------------ */

void ui_draw_top(UIState* ui, C3D_RenderTarget* target) {
    AudioState*    a = ui->audio;
    const Strings* s = &STR[ui->lang];

    // Clear the shared text buffer once at the start of the frame
    C2D_TextBufClear(ui->textbuf);

    // Header bar
    draw_rect(0, 0, SCREEN_W_TOP, 24, ui->col_accent);
    draw_text(ui, s->app_title, 8, 4, 0.55f, COL32(0xFF,0xFF,0xFF,0xFF));

    // Language indicator
    draw_rect(358, 3, 34, 18, COL32(0x00,0x00,0x00,0x88));
    draw_text(ui, s->lang_indicator, 365, 4, 0.52f, COL32(0xFF,0xFF,0xFF,0xFF));

    // Status
    const char* status_label = s->status_stopped;
    u32         status_col   = ui->col_dim;
    if (a->status == AUDIO_PLAYING) { status_label = s->status_playing; status_col = COL32(0x44,0xFF,0x88,0xFF); }
    if (a->status == AUDIO_PAUSED)  { status_label = s->status_paused;  status_col = COL32(0xFF,0xCC,0x44,0xFF); }
    draw_text(ui, status_label, 8, 30, 0.50f, status_col);

    // Track title
    if (a->status != AUDIO_STOPPED && a->current_title[0])
        draw_text(ui, a->current_title, 8, 44, 0.52f, ui->col_text);
    else
        draw_text(ui, s->no_track, 8, 44, 0.52f, ui->col_text);

    // Progress bar
    float prog = audio_progress(a);
    draw_rect(8, 62, SCREEN_W_TOP - 16, 6, ui->col_bar);
    draw_rect(8, 62, (SCREEN_W_TOP - 16) * prog, 6, ui->col_accent);
    draw_textf(ui, 8, 72, 0.42f, ui->col_dim, "%d:%02d / %d:%02d",
               (int)a->position/60, (int)a->position%60,
               (int)a->duration/60, (int)a->duration%60);

    // Waveform
    audio_get_waveform(a, ui->wave, 200);
    float wy = 108, wh = 30;
    draw_rect(0, wy - wh, SCREEN_W_TOP, wh * 2, COL32(0x1A,0x1A,0x30,0xFF));
    for (int i = 0; i < 200; i++) {
        float h = fabsf(ui->wave[i]) * wh;
        if (h < 1) h = 1;
        u32 wc = (a->status == AUDIO_PLAYING)
               ? COL32(0x7C, 0x3A, 0xFF, 0xCC)
               : COL32(0x44, 0x44, 0x66, 0x88);
        draw_rect(i * 2, wy - h, 1, h * 2, wc);
    }

    // Pitch
    draw_text(ui, s->pitch_label, 8, 148, 0.45f, ui->col_dim);
    draw_bar(55, 150, 110, 10, a->pitch, PITCH_MIN, PITCH_MAX,
             ui->col_bar, COL32(0xFF, 0x77, 0x44, 0xFF));
    draw_textf(ui, 172, 148, 0.45f, COL32(0xFF,0x99,0x66,0xFF), "%+.0f st", a->pitch);
    draw_text(ui, s->pitch_buttons, 8, 162, 0.38f, ui->col_dim);

    // Speed
    draw_text(ui, s->speed_label, 218, 148, 0.45f, ui->col_dim);
    draw_bar(262, 150, 110, 10, a->speed, SPEED_MIN, SPEED_MAX,
             ui->col_bar, COL32(0x44, 0xCC, 0xFF, 0xFF));
    draw_textf(ui, 378, 148, 0.45f, COL32(0x88,0xEE,0xFF,0xFF), "%.2fx", a->speed);
    draw_text(ui, s->speed_buttons, 218, 162, 0.38f, ui->col_dim);

    // Controls
    draw_rect(0, 192, SCREEN_W_TOP, 1, ui->col_bar);
    draw_text(ui, s->controls_line1, 6, 197, 0.38f, ui->col_dim);
    draw_text(ui, s->controls_line2, 6, 213, 0.38f, ui->col_dim);
}

/* ------------------------------------------------------------------ */
/*  Bottom screen                                                       */
/* ------------------------------------------------------------------ */

void ui_draw_bottom(UIState* ui, C3D_RenderTarget* target) {
    FileBrowser*   fb = ui->fb;
    const Strings* s  = &STR[ui->lang];

    // Note: textbuf was already cleared in ui_draw_top this frame.
    // Bottom screen is drawn in the same frame, so we just keep adding.

    // Header
    draw_rect(0, 0, SCREEN_W_BOT, 18, COL32(0x22, 0x22, 0x3A, 0xFF));
    const char* cwd_disp = fb->cwd;
    if (strlen(cwd_disp) > 38) cwd_disp = fb->cwd + strlen(fb->cwd) - 38;
    draw_text(ui, cwd_disp, 4, 2, 0.40f, ui->col_dim);

    if (fb->count == 0) {
        draw_text(ui, s->no_files_1, 8, 40, 0.48f, ui->col_dim);
        draw_text(ui, s->no_files_2, 8, 58, 0.42f, ui->col_dim);
        draw_text(ui, s->no_files_3, 8, 72, 0.42f, ui->col_dim);
        return;
    }

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = fb->scroll + i;
        if (idx >= fb->count) break;

        float ry = 20 + i * ROW_H;
        BrowserEntry* e = &fb->entries[idx];
        bool selected = (idx == fb->selected);

        if (selected)
            draw_rect(0, ry, SCREEN_W_BOT, ROW_H - 1, ui->col_sel);

        const char* icon    = e->is_dir ? "[Dir]" : "[F]";
        u32         icon_col = e->is_dir ? ui->col_dir : ui->col_dim;
        draw_text(ui, icon, 4, ry + 2, 0.42f, icon_col);

        // Truncate name safely
        char name_disp[40];
        size_t nl = strlen(e->name);
        if (nl > 36) {
            memcpy(name_disp, e->name, 33);
            memcpy(name_disp + 33, "...", 4);
        } else {
            memcpy(name_disp, e->name, nl + 1);
        }
        u32 name_col = selected ? ui->col_text
                                : (e->is_dir ? ui->col_dir : COL32(0xCC,0xCC,0xEE,0xFF));
        draw_text(ui, name_disp, 34, ry + 2, 0.44f, name_col);
    }

    // Scrollbar
    if (fb->count > VISIBLE_ROWS) {
        float sb_x    = SCREEN_W_BOT - 5;
        float sb_h    = SCREEN_H - 20;
        float thumb   = sb_h * VISIBLE_ROWS / fb->count;
        float thumb_y = 20 + sb_h * fb->scroll / fb->count;
        draw_rect(sb_x, 20, 4, sb_h, ui->col_bar);
        draw_rect(sb_x, thumb_y, 4, thumb, ui->col_accent);
    }

    draw_textf(ui, SCREEN_W_BOT - 40, SCREEN_H - 14, 0.38f, ui->col_dim,
               "%d/%d", fb->selected + 1, fb->count);
}
