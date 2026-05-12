#pragma once
#include <citro2d.h>
#include "audio.h"
#include "filebrowser.h"

typedef struct {
    AudioState*  audio;
    FileBrowser* fb;

    // Waveform visualizer data
    float        wave[200];

    // Cached colors
    u32          col_bg;
    u32          col_accent;
    u32          col_text;
    u32          col_dim;
    u32          col_sel;
    u32          col_dir;
    u32          col_bar;
} UIState;

void ui_init(UIState* ui, AudioState* audio, FileBrowser* fb);
void ui_draw_top(UIState* ui, C3D_RenderTarget* target);
void ui_draw_bottom(UIState* ui, C3D_RenderTarget* target);
