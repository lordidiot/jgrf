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

#include <jg/jg.h>

#include "parg.h"

#include "jgrf.h"
#include "settings.h"

// Variables for command line options
static const char *corename = NULL;
static int debug = 0;
static int fullscreen = 0;
static int scale = 0;
static int shader = -1;
static int windowed = 0;

// Return the core name specified at the command line
const char *jgrf_cli_core() {
    return corename;
}

// Override settings with command line arguments
void jgrf_cli_override() {
    settings_t *settings = jgrf_get_settings();
    
    if (debug) {
        settings->misc_corelog.val = 0;
        settings->misc_frontendlog.val = 0;
    }
    
    if (fullscreen)
        settings->video_fullscreen.val = 1;
    
    if (windowed)
        settings->video_fullscreen.val = 0;
    
    if (shader >= 0 && shader <= settings->video_shader.max)
        settings->video_shader.val = shader;
    
    if (scale && scale <= settings->video_scale.max)
        settings->video_scale.val = scale;
}

// Command line parsing
void jgrf_cli_parse(int argc, char *argv[]) {
    struct parg_state ps;
    int c;
    
    parg_init(&ps);
    
    while ((c = parg_getopt(&ps, argc, argv, "c:dfs:wx:")) != -1) {
        switch (c) {
            case 1:
                break;
            case 'c': // Core selection
                corename = ps.optarg;
                break;
            case 'd': // Enable debug level logs from core and frontend
                debug = 1;
                break;
            case 'f': // Start in fullscreen mode
                fullscreen = 1;
                break;
            case 's': // Shader
                shader = atoi(ps.optarg);
                break;
            case 'w': // Start in windowed mode
                windowed = 1;
                break;
            case 'x': // Scale
                scale = atoi(ps.optarg);
                break;
            case '?':
                jgrf_log(JG_LOG_WRN, "Unknown option '-%c'\n", ps.optopt);
                break;
            default:
                break;
        }
    }
}
