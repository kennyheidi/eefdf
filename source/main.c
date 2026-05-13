/*
 * 3DS Audio Player with Pitch & Speed Control
 * Supports: MP3, OGG, FLAC, WAV
 *
 * Changes for old 3DS:
 *   - __stacksize__ = 512 KB  (stb_vorbis IMDCT uses huge local arrays)
 *   - No consoleInit: it conflicts with C2D on the same screen — the console
 *     writes its background to one double-buffer slot, C2D renders to the
 *     other, and they alternate visibly as green corruption on the bottom screen.
 *     NDSP errors are shown via C2D text instead of printf.
 *   - Render targets created before NDSP check so C2D is available for
 *     the error screen without restructuring the flow.
 *   - Sleep mode support: Music continues playing when the lid is closed.
 */

#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <math.h>

#include "audio.h"
#include "filebrowser.h"
#include "ui.h"

/*
 * Override the libctru default stack of 32 KB.
 * stb_vorbis decode functions (IMDCT) allocate ~32 KB of local float arrays
 * per stereo channel per frame.
 */
u32 __stacksize__ = 0x80000; /* 512 KB */

/* ------------------------------------------------------------------ */
/*  Simple C2D error screen — used instead of printf when NDSP fails   */
/* ------------------------------------------------------------------ */

static void draw_ndsp_error(C3D_RenderTarget* top, C3D_RenderTarget* bottom,
                            C2D_TextBuf tbuf) {
    static const struct { const char* str; float y; u32 col; } lines[] = {
        { "Audio init failed!",           30,  0xFF4444FFu },
        { "DSP firmware not found.",      54,  0xEEEEFFFFu },
        { "",                             72,  0 },
        { "To fix:",                      82,  0xAAAACCFFu },
        { "1. Hold SELECT on boot",       100, 0xCCCCFFFFu },
        { "2. Open Rosalina menu",        118, 0xCCCCFFFFu },
        { "3. Miscellaneous options",     136, 0xCCCCFFFFu },
        { "4. Dump DSP firmware",         154, 0xCCCCFFFFu },
        { "5. Restart this app",          172, 0xCCCCFFFFu },
        { "",                             190, 0 },
        { "START = exit    A = continue", 205, 0x44FF88FFu },
    };

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(top, C2D_Color32(0x12, 0x12, 0x1E, 0xFF));
    C2D_SceneBegin(top);
    C2D_TextBufClear(tbuf);

    for (int i = 0; i < (int)(sizeof(lines)/sizeof(lines[0])); i++) {
        if (!lines[i].str[0]) continue;
        C2D_Text t;
        C2D_TextParse(&t, tbuf, lines[i].str);
        C2D_TextOptimize(&t);
        C2D_DrawText(&t, C2D_WithColor, 12.0f, lines[i].y, 0.5f,
                     0.5f, 0.5f, lines[i].col);
    }

    C2D_TargetClear(bottom, C2D_Color32(0x0E, 0x0E, 0x18, 0xFF));
    C2D_SceneBegin(bottom);

    C3D_FrameEnd(0);
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                       */
/* ------------------------------------------------------------------ */

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    /*
     * DO NOT call consoleInit here.
     * All debug output now goes through C2D instead.
     */

    romfsInit();
    cfguInit();

    /* Enable audio playback in sleep mode. */
    aptSetSleepAllowed(true);

    /* Create render targets for both screens. */
    C3D_RenderTarget* top    = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    /* Main program objects */
    AudioState  audio;
    FileBrowser fb;
    UIState     ui;

    /* Initialise audio (this will try ndspInit internally) */
    audio_init(&audio);

    /* If NDSP is not available, show error screen and optionally exit. */
    if (!audio.ndsp_ok) {
        C2D_TextBuf tbuf = C2D_TextBufNew(512);
        bool do_exit = false;

        while (aptMainLoop()) {
            hidScanInput();
            u32 kDown = hidKeysDown();

            if (kDown & KEY_START) { do_exit = true; break; }
            if (kDown & KEY_A) break;

            draw_ndsp_error(top, bottom, tbuf);
            gspWaitForVBlank();
        }

        C2D_TextBufDelete(tbuf);
        if (do_exit) goto cleanup_early;
    }

    filebrowser_init(&fb, "sdmc:/music");
    ui_init(&ui, &audio, &fb, NULL); /* Easter egg removed */

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        /* File browser
