/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

#include <SDL.h>
#include <jg/jg.h>

#include "lodepng.h"

#include "jgrf.h"
#include "video.h"
#include "video_gl.h"
#include "settings.h"

void (*jgrf_video_create)(void);
int (*jgrf_video_init)(void);
void (*jgrf_video_deinit)(void);
void (*jgrf_video_fullscreen)(void);
void (*jgrf_video_render)(int);
void (*jgrf_video_resize)(void);
void (*jgrf_video_get_scale_params)(float*, float*, float*, float*);
void (*jgrf_video_set_cursor)(int);
jg_videoinfo_t* (*jgrf_video_get_info)(void);
void (*jgrf_video_set_info)(jg_videoinfo_t*);
void (*jgrf_video_swapbuffers)(void);
void (*jgrf_video_text)(int, int, const char*);
void (*jgrf_video_rehash)(void);

static jgrf_gdata_t *gdata;

// Set function pointers for video - Use to select a video API when more exist
void jgrf_video_setfuncs(void) {
    gdata = jgrf_gdata_ptr();
    jg_setting_t *settings = jgrf_settings_ptr();

    switch (settings[VIDEO_API].val) {
        case 0: case 1: // OpenGL - Core Profile and OpenGL ES
            jgrf_video_create = &jgrf_video_gl_create;
            jgrf_video_init = &jgrf_video_gl_init;
            jgrf_video_deinit = &jgrf_video_gl_deinit;
            jgrf_video_fullscreen = &jgrf_video_gl_fullscreen;
            jgrf_video_render = &jgrf_video_gl_render;
            jgrf_video_resize = &jgrf_video_gl_resize;
            jgrf_video_get_scale_params = &jgrf_video_gl_get_scale_params;
            jgrf_video_set_cursor = &jgrf_video_gl_set_cursor;
            jgrf_video_get_info = &jgrf_video_gl_get_info;
            jgrf_video_set_info = &jgrf_video_gl_set_info;
            jgrf_video_swapbuffers = &jgrf_video_gl_swapbuffers;
            jgrf_video_text = &jgrf_video_gl_text;
            jgrf_video_rehash = &jgrf_video_gl_rehash;
            break;
        case 2: // OpenGL - Compatibility Profile
            jgrf_video_create = &jgrf_video_gl_create;
            jgrf_video_init = &jgrf_video_gl_init;
            jgrf_video_deinit = &jgrf_video_gl_deinit;
            jgrf_video_fullscreen = &jgrf_video_gl_fullscreen;
            jgrf_video_render = &jgrf_video_gl_render_compat;
            jgrf_video_resize = &jgrf_video_gl_resize;
            jgrf_video_get_scale_params = &jgrf_video_gl_get_scale_params;
            jgrf_video_set_cursor = &jgrf_video_gl_set_cursor;
            jgrf_video_get_info = &jgrf_video_gl_get_info;
            jgrf_video_set_info = &jgrf_video_gl_set_info;
            jgrf_video_swapbuffers = &jgrf_video_gl_swapbuffers;
            jgrf_video_text = &jgrf_video_gl_text;
            jgrf_video_rehash = &jgrf_video_gl_rehash;
            break;
        case 3: // Vulkan - one day...
        default:
            jgrf_log(JG_LOG_ERR, "Invalid Video API: %d\n",
                settings[VIDEO_API].val);
            break;
    }
}

// Load an application icon
void jgrf_video_icon_load(SDL_Window *window) {
    char iconpath[192];
#ifdef __APPLE__
    int iconsize = 1024;
#else
    int iconsize = 96;
#endif
    snprintf(iconpath, sizeof(iconpath), "icons/jollygood%d.png", iconsize);
#if defined(DATADIR)
    struct stat fbuf; // Make sure the icon actually exists at this path
    if (stat(iconpath, &fbuf) != 0) { // Not found locally, use system-wide path
        snprintf(iconpath, sizeof(iconpath),
            "%s/jollygood/jgrf/jollygood%d.png", DATADIR, iconsize);
    }
#endif

    uint32_t x, y;
    uint8_t *png_icon = 0;
    uint8_t error = lodepng_decode32_file(&png_icon, &x, &y, iconpath);
    if (error)
        jgrf_log(JG_LOG_WRN, "lodepng code %u: %s\n",
            error, lodepng_error_text(error));

    SDL_Surface *icon;
    // pixels, width, height, depth, pitch, rmask, gmask, bmask, amask
    icon = SDL_CreateRGBSurfaceFrom(png_icon, x, y, 32, x * sizeof(uint32_t),
        0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);

    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
    free(png_icon);

    // Seed the RNG
    srand((unsigned)time(NULL));
}

// Write the currently displayed video frame to a .png file
void jgrf_video_screenshot(void) {
    char ssname[256];
    snprintf(ssname, sizeof(ssname), "%s%d-%03x.png",
        gdata->sspath, (unsigned)time(NULL), rand() % 0xfff);

    // Rendered pixels after post-processing
    int rw, rh;
    void *ssdata = jgrf_video_gl_get_pixels(&rw, &rh);

    uint8_t error = lodepng_encode32_file(ssname, (const uint8_t*)ssdata,
        rw, rh);

    if (error)
        jgrf_log(JG_LOG_WRN, "lodepng code %u: %s\n",
            error, lodepng_error_text(error));

    free(ssdata);
}
