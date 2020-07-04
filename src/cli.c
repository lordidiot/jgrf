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
#include "cli.h"
#include "settings.h"

// Variables for command line options
static const char *os_def = "c:dfhs:wx:";
static const struct parg_option po_def[] = {
    { "core", PARG_REQARG, NULL, 'c' },
    { "debug", PARG_NOARG, NULL, 'd' },
    { "fullscreen", PARG_NOARG, NULL, 'f' },
    { "help", PARG_NOARG, NULL, 'h' },
    { "shader", PARG_REQARG, NULL, 's' },
    { "window", PARG_NOARG, NULL, 'w' },
    { "scale", PARG_REQARG, NULL, 'x' },
};

static const char *corename = NULL;
static int debug = 0;
static int fullscreen = 0;
static int scale = 0;
static int shader = -1;
static int windowed = 0;

// Return the core name specified at the command line
const char *jgrf_cli_core(void) {
    return corename;
}

// Override settings with command line arguments
void jgrf_cli_override(void) {
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
    int l = -1;
    
    parg_init(&ps);
    
    parg_reorder(argc, argv, os_def, po_def);
    
    while ((c = parg_getopt_long(&ps, argc, argv, os_def, po_def, &l)) != -1) {
        switch (c) {
            case 1:
                break;
            case 'c': // Core selection
                corename = ps.optarg;
                break;
            case 'd': // Enable debug level logs from core and frontend
                debug = 1;
                break;
            case 'h': // Show usage
                jgrf_cli_usage();
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
                //jgrf_log(JG_LOG_WRN, "Unknown option '-%c'\n", ps.optopt);
                break;
            default:
                break;
        }
    }
}

void jgrf_cli_usage(void) {
    fprintf(stdout, "usage: jollygood [options] game\n");
    fprintf(stdout, "  options:\n");
    fprintf(stdout, "    -c, --core <corename>   "
        "Specify which core to use\n");
    fprintf(stdout, "    -d, --debug             "
        "Enable debug log level for core and frontend\n");
    fprintf(stdout, "    -f, --fullscreen        "
        "Start in Fullscreen mode\n");
    fprintf(stdout, "    -h, --help              "
        "Show Usage\n");
    fprintf(stdout, "    -s, --shader <value>    "
        "Select a Post-processing shader\n");
    fprintf(stdout, "                              0 = Nearest Neighbour (None)"
        "\n");
    fprintf(stdout, "                              1 = Linear\n");
    fprintf(stdout, "                              2 = Sharp Bilinear\n");
    fprintf(stdout, "                              3 = AANN (Anti-Aliased "
        "Nearest Neighbour)\n");
    fprintf(stdout, "                              4 = CRT-Bespoke\n");
    fprintf(stdout, "                              5 = CRTea\n");
    fprintf(stdout, "                              6 = LCD\n");
    fprintf(stdout, "    -w, --window            "
        "Start in Windowed mode\n");
    fprintf(stdout, "    -x, --scale <value>     "
        "Video Scale Factor (1 to 8)\n\n");
    exit(EXIT_FAILURE);
}
