/*
Copyright (c) 2020-2022 Rupert Carmichael
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>

#include <jg/jg.h>

#include "parg.h"

#include "jgrf.h"
#include "cli.h"
#include "settings.h"

// Variables for command line options
static const char *os_def = "a:b:c:fg:ho:r:s:vwx:";
static const struct parg_option po_def[] = {
    { "video", PARG_REQARG, NULL, 'a' },
    { "benchmark", PARG_REQARG, NULL, 'b' },
    { "core", PARG_REQARG, NULL, 'c' },
    { "fullscreen", PARG_NOARG, NULL, 'f' },
    { "cheats", PARG_NOARG, NULL, 'g' },
    { "help", PARG_NOARG, NULL, 'h' },
    { "wave", PARG_REQARG, NULL, 'o' },
    { "rsqual", PARG_REQARG, NULL, 'r' },
    { "shader", PARG_REQARG, NULL, 's' },
    { "verbose", PARG_NOARG, NULL, 'v' },
    { "window", PARG_NOARG, NULL, 'w' },
    { "scale", PARG_REQARG, NULL, 'x' },
};

static const char *corename = NULL;
static const char *wavfile = NULL;

static int video = -1;
static int fullscreen = 0;
static int cheatauto = 0;
static int rsqual = -1;
static int scale = 0;
static int shader = -1;
static int verbose = 0;
static int windowed = 0;

int waveout = 0;

// Return the core name specified at the command line
const char *jgrf_cli_core(void) {
    return corename;
}

// Return the wave file name specified at the command line
const char *jgrf_cli_wave(void) {
    return wavfile;
}

// Override settings with command line arguments
void jgrf_cli_override(void) {
    jg_setting_t *settings = jgrf_settings_ptr();

    if (verbose) {
        settings[MISC_CORELOG].val = 0;
        settings[MISC_FRONTENDLOG].val = 0;
    }

    if (video >= 0 && video <= settings[VIDEO_API].max)
        settings[VIDEO_API].val = video;

    if (fullscreen)
        settings[VIDEO_FULLSCREEN].val = 1;

    if (cheatauto >= 0 && cheatauto <= settings[MISC_CHEATAUTO].max)
        settings[MISC_CHEATAUTO].val = cheatauto;

    if (windowed)
        settings[VIDEO_FULLSCREEN].val = 0;

    if (rsqual >= 0 && rsqual <= settings[AUDIO_RSQUAL].max)
        settings[AUDIO_RSQUAL].val = rsqual;

    if (shader >= 0 && shader <= settings[VIDEO_SHADER].max)
        settings[VIDEO_SHADER].val = shader;

    if (scale && scale <= settings[VIDEO_SCALE].max)
        settings[VIDEO_SCALE].val = scale;
}

// Command line parsing
void jgrf_cli_parse(int argc, char *argv[]) {
    struct parg_state ps;
    int c;
    int l = -1;

    parg_init(&ps);

    // Reorder the arguments to place non-options last
    int optend = parg_reorder(argc, argv, os_def, po_def);

    // Last non-option is the ROM filename
    jgrf_gdata_t *gdata = jgrf_gdata_ptr();
    gdata->filename = argv[argc - 1];

    // Count the number of auxiliary files
    gdata->numauxfiles = (unsigned)argc - optend - 1;
    if (gdata->numauxfiles > JGRF_AUXFILE_MAX)
        gdata->numauxfiles = JGRF_AUXFILE_MAX;

    for (int i = 0; i < gdata->numauxfiles; ++i)
        jgrf_auxfile_load(argv[optend + i], i);

    while ((c = parg_getopt_long(&ps, argc, argv, os_def, po_def, &l)) != -1) {
        switch (c) {
            case 1: {
                break;
            }
            case 'a': { // Video API
                video = atoi(ps.optarg);
                break;
            }
            case 'b': { // Benchmark
                jgrf_benchmark(atoll(ps.optarg));
                break;
            }
            case 'c': { // Core selection
                corename = ps.optarg;
                break;
            }
            case 'h': { // Show usage
                jgrf_cli_usage(argv[0]);
                jgrf_quit(EXIT_SUCCESS);
                break;
            }
            case 'f': { // Start in fullscreen mode
                fullscreen = 1;
                break;
            }
            case 'g': { // Auto-activate cheats if present
                cheatauto = atoi(ps.optarg);
                break;
            }
            case 'o': { // Wave File Output
                wavfile = ps.optarg;
                struct stat fbuf;
                if (stat(wavfile, &fbuf) == 0) {
                    jgrf_log(JG_LOG_WRN,
                        "WAV Writer: Refusing to overwrite %s!\n", wavfile);
                }
                else {
                    jgrf_log(JG_LOG_WRN,
                        "WAV Writer: Writing WAV to %s\n", wavfile);
                    waveout = 1;
                }
                break;
            }
            case 'r': { // Resampler Quality
                rsqual = atoi(ps.optarg);
                break;
            }
            case 's': { // Shader
                shader = atoi(ps.optarg);
                break;
            }
            case 'v': { // Enable verbose log output for core and frontend
                verbose = 1;
                break;
            }
            case 'w': { // Start in windowed mode
                windowed = 1;
                break;
            }
            case 'x': { // Scale
                scale = atoi(ps.optarg);
                break;
            }
            case '?': {
                jgrf_log(JG_LOG_WRN, "Unknown option '-%c'\n", ps.optopt);
                break;
            }
            default: {
                break;
            }
        }
    }
}

void jgrf_cli_usage(char *binname) {
#ifdef JGRF_STATIC
    jg_coreinfo_t *coreinfo = jg_get_coreinfo("static");
    fprintf(stdout, "%s %s\n", coreinfo->fname, coreinfo->version);
#else
    fprintf(stdout, "The Jolly Good Reference Frontend %s\n", VERSION);
#endif
    fprintf(stdout, "usage: %s [options] [auxiliary files] game\n", binname);
    fprintf(stdout, "  options:\n");
    fprintf(stdout, "    -a, --video <value>     "
        "Specify which Video API to use\n");
    fprintf(stdout, "                              0 = OpenGL Core Profile\n");
    fprintf(stdout, "                              1 = OpenGL ES\n");
    fprintf(stdout, "                              2 = OpenGL Compatibility "
        "Profile\n");
    fprintf(stdout, "    -b, --bmark <frames>    "
        "Run N frames in Benchmark mode\n");
    fprintf(stdout, "    -c, --core <corename>   "
        "Specify which core to use\n");
    fprintf(stdout, "    -f, --fullscreen        "
        "Start in Fullscreen mode\n");
    fprintf(stdout, "    -g, --cheats <value>    "
        "Auto-activate cheats if present\n");
    fprintf(stdout, "                              0 = Disabled\n");
    fprintf(stdout, "                              1 = Enabled\n");
    fprintf(stdout, "    -h, --help              "
        "Show Usage\n");
    fprintf(stdout, "    -o, --wave <filename>   "
        "Specify WAV output file\n");
    fprintf(stdout, "    -r, --rsqual <value>    "
        "Resampler Quality (0 to 10)\n");
    fprintf(stdout, "    -s, --shader <value>    "
        "Select a Post-processing shader\n");
    fprintf(stdout, "                              0 = Nearest Neighbour (None)"
        "\n");
    fprintf(stdout, "                              1 = Linear\n");
    fprintf(stdout, "                              2 = Sharp Bilinear\n");
    fprintf(stdout, "                              3 = Anti-Aliased Nearest "
        "Neighbour\n");
    fprintf(stdout, "                              4 = CRT-Yee64\n");
    fprintf(stdout, "                              5 = CRTea\n");
    fprintf(stdout, "                              6 = LCD\n");
    fprintf(stdout, "    -v, --verbose           "
        "Enable verbose log output for core and frontend\n");
    fprintf(stdout, "    -w, --window            "
        "Start in Windowed mode\n");
    fprintf(stdout, "    -x, --scale <value>     "
        "Video Scale Factor (1 to 8)\n\n");
}
