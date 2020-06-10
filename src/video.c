/*
 * Copyright (c) 2020 Rupert Carmichael
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

void (*jgrf_video_create)();
void (*jgrf_video_init)();
void (*jgrf_video_deinit)();
void (*jgrf_video_fullscreen)();
void (*jgrf_video_render)();
void (*jgrf_video_resize)();
void (*jgrf_video_get_scale_params)(float*, float*, float*, float*);
void (*jgrf_video_set_cursor)(int);
jg_videoinfo_t* (*jgrf_video_get_info)();
void (*jgrf_video_set_info)(jg_videoinfo_t*);
void (*jgrf_video_swapbuffers)();
void (*jgrf_video_text)(int, int, const char*);

static jgrf_gdata_t *gdata;

// Set function pointers for video - Use to select a video API when more exist
void jgrf_video_setfuncs() {
    settings_t *settings = jgrf_get_settings();
    gdata = jgrf_gdata_ptr();
    
    switch (settings->video_api.val) {
        case 0: // OpenGL
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
            break;
        case 1: // Vulkan - one day...
        default:
            jgrf_log(JG_LOG_ERR, "Invalid Video API: %d\n",
                settings->video_api.val);
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
        snprintf(iconpath, sizeof(iconpath), "%s/jgrf/jollygood%d.png",
            DATADIR, iconsize);
    }
#endif
    
    uint32_t x, y;
    uint8_t *png_icon = 0;
    uint8_t error = lodepng_decode32_file(&png_icon, &x, &y, iconpath);
    if (error) jgrf_log(JG_LOG_WRN, "lodepng code %u: %s\n",
        error, lodepng_error_text(error));
    
    SDL_Surface *icon;
    // pixels, width, height, depth, pitch, rmask, gmask, bmask, amask
    icon = SDL_CreateRGBSurfaceFrom(png_icon, x, y, 32, x * sizeof(uint32_t),
        0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
    
    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
    free(png_icon);
}

// Write the currently displayed video frame to a .png file
void jgrf_video_screenshot() {
    char ssname[256];
    snprintf(ssname, sizeof(ssname), "%s%ld-%03d.png",
        gdata->sspath, time(NULL), rand() % 899);
    
    // Rendered pixels after post-processing
    int rw, rh;
    void *ssdata = jgrf_video_gl_get_pixels(&rw, &rh);
    
    uint8_t error = lodepng_encode32_file(ssname, (const unsigned char*)ssdata,
        rw, rh);
    
    if (error) jgrf_log(JG_LOG_WRN,
        "lodepng code %u: %s\n", error, lodepng_error_text(error));
    
    free(ssdata);
}
