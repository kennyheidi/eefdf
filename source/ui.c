#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Enough glyph slots for all text drawn in one frame (top + bottom screen).
   Top screen:  ~350 chars worst-case (title + time + labels + hints)
   Bottom screen: ~500 chars worst-case (12 rows × ~40 chars + header)
   2048 is a comfortable ceiling with no per-frame allocation.            */
#define TEXT_BUF_GLYPHS 2048

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

#define COL32(r,g,b,a) C2D_Color32(r,g,b,a)
#define SCREEN_W_TOP   400
#define SCREEN_W_BOT   320
#define SCREEN_H       240
#define ROW_H           18

static void draw_rect(float x, float y, float w, float h, u32 col) {
    C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
}

/*
 * draw_text — uses the pre-allocated shared_buf from UIState.
 * The caller must have called C2D_TextBufClear(ui->shared_buf) once at the
 * start of the frame; individual calls here just parse into available slots.
 */
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
/*  Init / fini                                                         */
/* ------------------------------------------------------------------ */

void ui_init(UIState* ui, AudioState* audio, FileBrowser* fb) {
    memset(ui, 0, sizeof(*ui));
    ui->audio = audio;
    ui->fb    = fb;

    /* Allocate the shared text buffer ONCE — never freed until ui_fini() */
    ui->shared_buf = C2D_TextBufNew(TEXT_BUF_GLYPHS);

    ui->col_bg     = COL32(0x12, 0x12, 0x1E, 0xFF);
    ui->col_accent = COL32(0x7C, 0x3A, 0xFF, 0xFF); // purple
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
