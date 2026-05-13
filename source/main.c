/*
 * 3DS Audio Player — defensive init version
 * Checks every service init and shows error on screen instead of crashing.
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

// Show a fatal error on the bottom screen and wait for START
static void fatal(const char* msg, Result code) {
    // consoleinit may not be up yet — try to set it up bare
    consoleInit(GFX_BOTTOM, NULL);
    printf("\x1b[1;1H"); // move to top-left
    printf("\x1b[31m== FATAL ERROR ==\x1b[0m\n");
    printf("%s\n", msg);
    if (code) printf("Code: 0x%08lX\n", code);
    printf("\nPress START to exit.");
    while (aptMainLoop()) {
        gfxSwapBuffers();
        gfxFlushBuffers();
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
    }
}

int main(void) {
    // Step 1: gfx — must be first
    gfxInitDefault();
    gfxSet3D(false);

    // Step 2: console on bottom for early error messages
    consoleInit(GFX_BOTTOM, NULL);
    printf("Starting...\n");

    // Step 3: romfs (non-fatal if missing)
    romfsInit();

    // Step 4: cfgu
    Result rc = cfguInit();
    if (R_FAILED(rc)) {
        printf("cfguInit failed: 0x%08lX (continuing)\n", rc);
    }

    // Step 5: citro3d
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        fatal("C3D_Init failed.", 0);
        cfguExit();
        romfsExit();
        gfxExit();
        return 1;
    }

    // Step 6: citro2d
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        fatal("C2D_Init failed.", 0);
        C3D_Fini();
        cfguExit();
        romfsExit();
        gfxExit();
        return 1;
    }
    C2D_Prepare();

    // Step 7: ndsp — check for dspfirm.cdc first
    bool ndsp_ok = false;
    {
        // Probe for firmware file before calling ndspInit
        // to avoid the svcBreak assert when it's missing
        FILE* fw = fopen("sdmc:/3ds/dspfirm.cdc", "rb");
        if (!fw) {
            printf("\x1b[33mWARN: dspfirm.cdc not found\x1b[0m\n");
            printf("Audio disabled.\n");
            printf("Hold SELECT on boot ->\n");
            printf("Rosalina -> Misc ->\n");
            printf("Dump DSP firmware\n\n");
            printf("Press A to continue\n");
            printf("Press START to exit\n");
            while (aptMainLoop()) {
                gfxSwapBuffers();
                gfxFlushBuffers();
                hidScanInput();
                u32 k = hidKeysDown();
                if (k & KEY_START) {
                    C2D_Fini(); C3D_Fini();
                    cfguExit(); romfsExit(); gfxExit();
                    return 0;
                }
                if (k & KEY_A) break;
            }
        } else {
            fclose(fw);
            rc = ndspInit();
            if (R_FAILED(rc)) {
                printf("\x1b[33mWARN: ndspInit: 0x%08lX\x1b[0m\n", rc);
                printf("Audio disabled.\n");
                printf("Press A to continue.\n");
                while (aptMainLoop()) {
                    gfxSwapBuffers();
                    gfxFlushBuffers();
                    hidScanInput();
                    if (hidKeysDown() & KEY_A) break;
                }
            } else {
                ndsp_ok = true;
                printf("Audio OK\n");
            }
        }
    }

    printf("Loading UI...\n");
    gfxSwapBuffers();

    // Step 8: render targets
    C3D_RenderTarget* top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    if (!top || !bottom) {
        fatal("Failed to create render targets.", 0);
        if (ndsp_ok) ndspExit();
        C2D_Fini(); C3D_Fini();
        cfguExit(); romfsExit(); gfxExit();
        return 1;
    }

    // Step 9: app subsystems
    AudioState  audio;
    FileBrowser fb;
    UIState     ui;

    audio_init(&audio);
    audio.ndsp_available = ndsp_ok;
    filebrowser_init(&fb, "sdmc:/music");
    ui_init(&ui, &audio, &fb);

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_DUP)   filebrowser_move(&fb, -1);
        if (kDown & KEY_DDOWN) filebrowser_move(&fb,  1);
        if (kDown & KEY_B)     filebrowser_go_up(&fb);

        if (kDown & KEY_A) {
            BrowserEntry* entry = filebrowser_selected(&fb);
            if (entry) {
                if (entry->is_dir)  filebrowser_enter(&fb);
                else if (ndsp_ok)   audio_play(&audio, entry->full_path);
            }
        }

        if (kDown & KEY_START)  audio_stop(&audio);
        if (kDown & KEY_SELECT) audio_toggle_pause(&audio);
        if (kDown & KEY_X)      audio_reset_fx(&audio);
        if (kDown & KEY_Y)      ui_cycle_language(&ui);

        float speed_step = (kHeld & (KEY_L | KEY_R)) ? 0.1f : 0.05f;
        if (kDown & KEY_DLEFT)  audio_adjust_speed(&audio, -speed_step);
        if (kDown & KEY_DRIGHT) audio_adjust_speed(&audio,  speed_step);
        if (kDown & KEY_L)      audio_adjust_pitch(&audio, -1.0f);
        if (kDown & KEY_R)      audio_adjust_pitch(&audio,  1.0f);

        if (ndsp_ok) audio_update(&audio);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top,    C2D_Color32(0x12, 0x12, 0x1E, 0xFF));
        C2D_SceneBegin(top);
        ui_draw_top(&ui, top);
        C2D_TargetClear(bottom, C2D_Color32(0x0E, 0x0E, 0x18, 0xFF));
        C2D_SceneBegin(bottom);
        ui_draw_bottom(&ui, bottom);
        C3D_FrameEnd(0);
    }

    // Cleanup
    audio_shutdown(&audio);
        ui_free(&ui);
    filebrowser_free(&fb);
    C2D_Fini();
    C3D_Fini();
    if (ndsp_ok) ndspExit();
    cfguExit();
    romfsExit();
    gfxExit();
    return 0;
}
