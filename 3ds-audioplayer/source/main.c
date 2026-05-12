/*
 * 3DS Audio Player with Pitch & Speed Control
 * Supports: MP3, OGG, FLAC, WAV
 * Controls:
 *   D-Pad Up/Down    - Navigate file browser
 *   A                - Select file / Play
 *   B                - Go up a directory
 *   Left/Right       - Adjust speed (0.25x - 4.0x)
 *   L/R              - Adjust pitch (-12 to +12 semitones)
 *   Start            - Stop playback
 *   Select           - Toggle pause/resume
 *   X                - Reset pitch and speed
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

int main(void) {
    // Init services
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    consoleInit(GFX_BOTTOM, NULL);

    romfsInit();
    ndspInit();
    cfguInit();

    // Init subsystems
    AudioState audio;
    FileBrowser fb;
    UIState ui;

    audio_init(&audio);
    filebrowser_init(&fb, "sdmc:/music");
    ui_init(&ui, &audio, &fb);

    // Render targets
    C3D_RenderTarget* top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        // --- Input handling ---

        // File browser navigation
        if (kDown & KEY_DUP)   filebrowser_move(&fb, -1);
        if (kDown & KEY_DDOWN) filebrowser_move(&fb,  1);
        if (kDown & KEY_B)     filebrowser_go_up(&fb);

        if (kDown & KEY_A) {
            BrowserEntry* entry = filebrowser_selected(&fb);
            if (entry) {
                if (entry->is_dir) {
                    filebrowser_enter(&fb);
                } else {
                    audio_play(&audio, entry->full_path);
                }
            }
        }

        // Playback control
        if (kDown & KEY_START)  audio_stop(&audio);
        if (kDown & KEY_SELECT) audio_toggle_pause(&audio);
        if (kDown & KEY_X)      audio_reset_fx(&audio);

        // Speed control (Left/Right, held = faster change)
        float speed_step = (kHeld & (KEY_L | KEY_R)) ? 0.1f : 0.05f;
        if (kDown & KEY_DLEFT)  audio_adjust_speed(&audio, -speed_step);
        if (kDown & KEY_DRIGHT) audio_adjust_speed(&audio,  speed_step);

        // Pitch control (L/R buttons)
        if (kDown & KEY_L) audio_adjust_pitch(&audio, -1.0f);
        if (kDown & KEY_R) audio_adjust_pitch(&audio,  1.0f);

        // Update audio engine (processes pitch/speed changes)
        audio_update(&audio);

        // --- Rendering ---
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        // Top screen: Now Playing + visualizer
        C2D_TargetClear(top, C2D_Color32(0x12, 0x12, 0x1E, 0xFF));
        C2D_SceneBegin(top);
        ui_draw_top(&ui, top);

        // Bottom screen: File browser
        C2D_TargetClear(bottom, C2D_Color32(0x0E, 0x0E, 0x18, 0xFF));
        C2D_SceneBegin(bottom);
        ui_draw_bottom(&ui, bottom);

        C3D_FrameEnd(0);
    }

    // Cleanup
    audio_shutdown(&audio);
    filebrowser_free(&fb);
    C2D_Fini();
    C3D_Fini();
    ndspExit();
    cfguExit();
    romfsExit();
    gfxExit();

    return 0;
}
