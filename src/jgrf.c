/*
Copyright (c) 2020-2023 Rupert Carmichael
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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>


#include <SDL.h>
#include <jg/jg.h>

#include "md5.h"
#include "miniz.h"
#include "musl_memmem.h"

#include "jgrf.h"
#include "audio.h"
#include "cheats.h"
#include "cli.h"
#include "menu.h"
#include "input.h"
#include "settings.h"
#include "video.h"

// Jolly Good API calls
static struct _jgapi {
    void *handle;
    // Function Pointers
    int (*jg_init)(void);
    void (*jg_deinit)(void);
    void (*jg_reset)(int);
    void (*jg_exec_frame)(void);
    int (*jg_game_load)(void);
    int (*jg_game_unload)(void);
    int (*jg_state_load)(const char*);
    void (*jg_state_load_raw)(const void*);
    int (*jg_state_save)(const char*);
    const void* (*jg_state_save_raw)(void);
    size_t (*jg_state_size)(void);
    void (*jg_media_select)(void);
    void (*jg_media_insert)(void);
    void (*jg_cheat_clear)(void);
    void (*jg_cheat_set)(const char*);
    void (*jg_rehash)(void);
    void (*jg_data_push)(uint32_t, int, const void*, size_t);
    // Callback Setup
    void (*jg_set_cb_log)(jg_cb_log_t);
    void (*jg_set_cb_audio)(jg_cb_audio_t);
    void (*jg_set_cb_frametime)(jg_cb_frametime_t);
    void (*jg_set_cb_rumble)(jg_cb_rumble_t);
    // Retrieve "info" structs from the core
    jg_coreinfo_t* (*jg_get_coreinfo)(const char*);
    jg_videoinfo_t* (*jg_get_videoinfo)(void);
    jg_audioinfo_t* (*jg_get_audioinfo)(void);
    jg_inputinfo_t* (*jg_get_inputinfo)(int);
    jg_setting_t* (*jg_get_settings)(size_t*);
    // Core setup
    void (*jg_setup_video)(void);
    void (*jg_setup_audio)(void);
    void (*jg_set_inputstate)(jg_inputstate_t*, int);
    void (*jg_set_gameinfo)(jg_fileinfo_t);
    void (*jg_set_auxinfo)(jg_fileinfo_t, int);
    void (*jg_set_paths)(jg_pathinfo_t);
} jgapi;

// Keep track of which internal systems have been loaded successfully
static struct _loaded {
    int core;
    int game;
    int audio;
    int video;
    int input;
    int settings;
    int policy;
} loaded = { 0, 0, 0, 0, 0, 0, 0 };

// Pointer to core info struct
static jg_coreinfo_t *coreinfo = NULL;

// Frontend knows the game and path info and passes this to the core
static jg_fileinfo_t gameinfo;
static jg_fileinfo_t auxinfo[JGRF_AUXFILE_MAX];
static jg_pathinfo_t pathinfo;

// Global data struct for miscellaneous information
static jgrf_gdata_t gdata;
jgrf_gdata_t *jgrf_gdata_ptr(void) {
    return &gdata;
}

// Program should keep running (1) or, shut down (0)
static int running = 1;

// Benchmark mode
int bmark = 0;

// Number of extra frames to run for fast-forwarding purposes
int fforward = 0;

// Frame timing
int corefps = 60;
int screenfps = 60;
size_t framecount = 0;
size_t bmarkframes = 0;

// Policy variables
int policy_sock = -1;
void (*stored_jg_exec_frame)(void) = NULL;
static jg_videoinfo_t *vidinfo = NULL;

// Recursive mkdir (similar to mkdir -p)
static void mkdirr(const char *dir) {
    char tmp[256];
    char *p = NULL;

    size_t len = snprintf(tmp, sizeof(tmp), "%s", dir);

    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;

    for (p = tmp + 1; *p; ++p) {
        if (*p == SEP) {
            *p = 0;
            #if defined(__MINGW32__) || defined(__MINGW64__)
            mkdir(tmp);
            #else
            mkdir(tmp, S_IRWXU);
            #endif
            *p = SEP;
        }
    }
    #if defined(__MINGW32__) || defined(__MINGW64__)
    mkdir(tmp);
    #else
    mkdir(tmp, S_IRWXU);
    #endif
}

// Create user directories
static void jgrf_mkdirs(void) {
    mkdirr(gdata.configpath);
    mkdirr(gdata.datapath);
    mkdirr(gdata.biospath);
    mkdirr(gdata.userassets);
    mkdirr(gdata.cheatpath);
    mkdirr(gdata.savepath);
    mkdirr(gdata.statepath);
    mkdirr(gdata.sspath);
}

// Tell the emulator core about the paths the frontend knows
static void jgrf_set_paths(void) {
    jgapi.jg_set_paths(pathinfo);
}

// Discern the name of the game, with and without file extension
static void jgrf_gamename(const char *filename) {
    // Set game's name based on the path
    snprintf(gdata.gamename, sizeof(gdata.gamename),
        "%s", basename((char*)filename));
    snprintf(gdata.gamefname, sizeof(gdata.gamefname),
        "%s", basename((char*)filename));

    // Strip the file extension off
    for (int i = strlen(gdata.gamename) - 1; i > 0; --i) {
        if (gdata.gamename[i] == '.') {
            gdata.gamename[i] = '\0';
            break;
        }
    }
    gameinfo.fname = gdata.gamefname;
    gameinfo.name = gdata.gamename;
}

// Handle log output from the frontend
void jgrf_log(int level, const char *fmt, ...) {
    va_list va;
    char buffer[512];
    static const char *lchr = "diwe";
    static const char *lcol[4] = {
        "\033[0;35m", "\033[0m", "\033[0;33m", "\033[1;31m"
    };

    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    va_end(va);

    FILE *fout = level == 1 ? stdout : stderr;

    jg_setting_t *settings = jgrf_settings_ptr();
    if (level == JG_LOG_SCR) {
        jgrf_video_text(0, corefps, buffer);
    }
    else if (level >= settings[MISC_FRONTENDLOG].val) {
        fprintf(fout, "%s%c: %s\033[0m", lcol[level], lchr[level], buffer);
        fflush(fout);
    }

    if (level == JG_LOG_ERR)
        jgrf_quit(EXIT_FAILURE);
}

// Handle log output from the core
static void jgrf_core_log(int level, const char *fmt, ...) {
    va_list va;
    char buffer[512];
    static const char *lchr = "DIWE";
    static const char *lcol[4] = {
        "\033[0;35m", "\033[0;36m", "\033[7;33m", "\033[1;7;31m"
    };

    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    va_end(va);

    FILE *fout = level == 1 ? stdout : stderr;

    jg_setting_t *settings = jgrf_settings_ptr();
    if (level == JG_LOG_SCR) {
        jgrf_video_text(1, corefps, buffer);
    }
    else if (level >= settings[MISC_CORELOG].val) {
        fprintf(fout, "%s%c: %s\033[0m", lcol[level], lchr[level], buffer);
        fflush(fout);
    }

    if (level == JG_LOG_ERR)
        jgrf_quit(EXIT_FAILURE);
}

// Pass callbacks into the core
static void jgrf_callbacks_set(void) {
    jgapi.jg_set_cb_log(&jgrf_core_log);
    jgapi.jg_set_cb_audio(&jgrf_audio_cb_core);
    jgapi.jg_set_cb_frametime(&jgrf_frametime);
    jgapi.jg_set_cb_rumble(&jgrf_input_rumble);
}

// Load and set up the core
static void jgrf_core_load(const char *corepath) {
    memset(&jgapi, 0, sizeof(jgapi));
#ifdef JGRF_STATIC
    (void)corepath;
    jgapi.jg_init = &jg_init;
    jgapi.jg_deinit = &jg_deinit;
    jgapi.jg_reset = &jg_reset;
    jgapi.jg_exec_frame = &jg_exec_frame;
    jgapi.jg_game_load = &jg_game_load;
    jgapi.jg_game_unload = &jg_game_unload;
    jgapi.jg_state_load = &jg_state_load;
    jgapi.jg_state_load_raw = &jg_state_load_raw;
    jgapi.jg_state_save = &jg_state_save;
    jgapi.jg_state_save_raw = &jg_state_save_raw;
    jgapi.jg_state_size = &jg_state_size;
    jgapi.jg_media_select = &jg_media_select;
    jgapi.jg_media_insert = &jg_media_insert;
    jgapi.jg_cheat_clear = &jg_cheat_clear;
    jgapi.jg_cheat_set = &jg_cheat_set;
    jgapi.jg_rehash = &jg_rehash;
    jgapi.jg_data_push = &jg_data_push;

    jgapi.jg_get_coreinfo = &jg_get_coreinfo;
    jgapi.jg_get_audioinfo = &jg_get_audioinfo;
    jgapi.jg_get_videoinfo = &jg_get_videoinfo;
    jgapi.jg_get_inputinfo = &jg_get_inputinfo;
    jgapi.jg_get_settings = &jg_get_settings;

    jgapi.jg_setup_video = &jg_setup_video;
    jgapi.jg_setup_audio = &jg_setup_audio;
    jgapi.jg_set_inputstate = &jg_set_inputstate;
    jgapi.jg_set_gameinfo = &jg_set_gameinfo;
    jgapi.jg_set_auxinfo = &jg_set_auxinfo;
    jgapi.jg_set_paths = &jg_set_paths;

    jgapi.jg_set_cb_log = &jg_set_cb_log;
    jgapi.jg_set_cb_audio = &jg_set_cb_audio;
    jgapi.jg_set_cb_frametime = &jg_set_cb_frametime;
    jgapi.jg_set_cb_rumble = &jg_set_cb_rumble;
#else
    jgapi.handle = SDL_LoadObject(corepath);

    if (!jgapi.handle)
        jgrf_log(JG_LOG_ERR, "%s\n", SDL_GetError());

    SDL_GetError();

    *(void**)(&jgapi.jg_init) = SDL_LoadFunction(jgapi.handle, "jg_init");
    *(void**)(&jgapi.jg_deinit) = SDL_LoadFunction(jgapi.handle, "jg_deinit");
    *(void**)(&jgapi.jg_reset) = SDL_LoadFunction(jgapi.handle, "jg_reset");
    *(void**)(&jgapi.jg_exec_frame) = SDL_LoadFunction(jgapi.handle,
        "jg_exec_frame");
    *(void**)(&jgapi.jg_game_load) = SDL_LoadFunction(jgapi.handle,
        "jg_game_load");
    *(void**)(&jgapi.jg_game_unload) = SDL_LoadFunction(jgapi.handle,
        "jg_game_unload");
    *(void**)(&jgapi.jg_state_load) = SDL_LoadFunction(jgapi.handle,
        "jg_state_load");
    *(void**)(&jgapi.jg_state_load_raw) = SDL_LoadFunction(jgapi.handle,
        "jg_state_load_raw");
    *(void**)(&jgapi.jg_state_save) = SDL_LoadFunction(jgapi.handle,
        "jg_state_save");
    *(void**)(&jgapi.jg_state_save_raw) = SDL_LoadFunction(jgapi.handle,
        "jg_state_save_raw");
    *(void**)(&jgapi.jg_state_size) = SDL_LoadFunction(jgapi.handle,
        "jg_state_size");
    *(void**)(&jgapi.jg_media_select) = SDL_LoadFunction(jgapi.handle,
        "jg_media_select");
    *(void**)(&jgapi.jg_media_insert) = SDL_LoadFunction(jgapi.handle,
        "jg_media_insert");
    *(void**)(&jgapi.jg_cheat_clear) = SDL_LoadFunction(jgapi.handle,
        "jg_cheat_clear");
    *(void**)(&jgapi.jg_cheat_set) = SDL_LoadFunction(jgapi.handle,
        "jg_cheat_set");
    *(void**)(&jgapi.jg_rehash) = SDL_LoadFunction(jgapi.handle,
        "jg_rehash");
    *(void**)(&jgapi.jg_data_push) = SDL_LoadFunction(jgapi.handle,
        "jg_data_push");

    *(void**)(&jgapi.jg_get_coreinfo) = SDL_LoadFunction(jgapi.handle,
        "jg_get_coreinfo");
    *(void**)(&jgapi.jg_get_audioinfo) = SDL_LoadFunction(jgapi.handle,
        "jg_get_audioinfo");
    *(void**)(&jgapi.jg_get_videoinfo) = SDL_LoadFunction(jgapi.handle,
        "jg_get_videoinfo");
    *(void**)(&jgapi.jg_get_inputinfo) = SDL_LoadFunction(jgapi.handle,
        "jg_get_inputinfo");
    *(void**)(&jgapi.jg_get_settings) = SDL_LoadFunction(jgapi.handle,
        "jg_get_settings");

    *(void**)(&jgapi.jg_setup_video) = SDL_LoadFunction(jgapi.handle,
        "jg_setup_video");
    *(void**)(&jgapi.jg_setup_audio) = SDL_LoadFunction(jgapi.handle,
        "jg_setup_audio");
    *(void**)(&jgapi.jg_set_inputstate) = SDL_LoadFunction(jgapi.handle,
        "jg_set_inputstate");
    *(void**)(&jgapi.jg_set_gameinfo) = SDL_LoadFunction(jgapi.handle,
        "jg_set_gameinfo");
    *(void**)(&jgapi.jg_set_auxinfo) = SDL_LoadFunction(jgapi.handle,
        "jg_set_auxinfo");
    *(void**)(&jgapi.jg_set_paths) = SDL_LoadFunction(jgapi.handle,
        "jg_set_paths");

    *(void**)(&jgapi.jg_set_cb_log) = SDL_LoadFunction(jgapi.handle,
        "jg_set_cb_log");
    *(void**)(&jgapi.jg_set_cb_audio) = SDL_LoadFunction(jgapi.handle,
        "jg_set_cb_audio");
    *(void**)(&jgapi.jg_set_cb_frametime) = SDL_LoadFunction(jgapi.handle,
        "jg_set_cb_frametime");
    *(void**)(&jgapi.jg_set_cb_rumble) = SDL_LoadFunction(jgapi.handle,
        "jg_set_cb_rumble");
#endif

    // Set up values in global data struct
    coreinfo = jgapi.jg_get_coreinfo(gdata.sys);

    // Read emulator settings
    jgrf_settings_emu(jgapi.jg_get_settings);

    // Match gdata.sys with coreinfo->sys in case of user overriding core at CLI
    snprintf(gdata.sys, sizeof(gdata.sys), "%s", coreinfo->sys);

    // Populate frontend's global data with information from the core
    snprintf(gdata.corefname, sizeof(gdata.corefname),
        "%s", coreinfo->fname);
    snprintf(gdata.coreversion, sizeof(gdata.coreversion),
        "%s", coreinfo->version);
#ifdef JGRF_STATIC
    snprintf(gdata.userassets, sizeof(gdata.userassets),
        "%sassets", gdata.datapath);
    snprintf(gdata.biospath, sizeof(gdata.biospath),
        "%sbios", gdata.datapath);
    snprintf(gdata.cheatpath, sizeof(gdata.cheatpath),
        "%scheats", gdata.datapath);
    snprintf(gdata.savepath, sizeof(gdata.savepath),
        "%ssave", gdata.datapath);
    snprintf(gdata.statepath, sizeof(gdata.statepath),
        "%sstate", gdata.datapath);
    gdata.numinputs = coreinfo->numinputs;
#else
    snprintf(gdata.userassets, sizeof(gdata.userassets),
        "%sassets%c%s", gdata.datapath, SEP, coreinfo->name);
    snprintf(gdata.biospath, sizeof(gdata.biospath),
        "%sbios", gdata.datapath);
    snprintf(gdata.cheatpath, sizeof(gdata.cheatpath),
        "%scheats%c%s", gdata.datapath, SEP, coreinfo->name);
    snprintf(gdata.savepath, sizeof(gdata.savepath),
        "%ssave%c%s", gdata.datapath, SEP, coreinfo->name);
    snprintf(gdata.statepath, sizeof(gdata.statepath),
        "%sstate%c%s", gdata.datapath, SEP, coreinfo->name);
    gdata.numinputs = coreinfo->numinputs;
#endif

    // Copy path values into the pathinfo struct
    pathinfo.base = gdata.datapath;
    pathinfo.core = gdata.coreassets;
    pathinfo.user = gdata.userassets;
    pathinfo.bios = gdata.biospath;
    pathinfo.save = gdata.savepath;

    // Pass function pointers for callbacks from core to frontend
    jgrf_callbacks_set();

    // Set paths in the core
    jgrf_set_paths();

    // Set up the videoinfo/audioinfo pointers in the frontend
    vidinfo = jgapi.jg_get_videoinfo();
    jgrf_menu_set_vinfo(jgapi.jg_get_videoinfo());
    jgrf_video_set_info(jgapi.jg_get_videoinfo());
    jgrf_audio_set_info(jgapi.jg_get_audioinfo());

    // Hand pointers to input states to the core
    jgrf_input_set_states(jgapi.jg_set_inputstate);

    // Initialize the emulator core
    loaded.core = jgapi.jg_init();
    if (!loaded.core)
        jgrf_log(JG_LOG_ERR, "Failed to initialize core. Exiting...\n");
}

// Unload the core
static void jgrf_core_unload(void) {
    jgapi.jg_deinit();

    if (jgapi.handle)
        SDL_UnloadObject(jgapi.handle);
}

// Generate the CRC32 checksum of the game data
static void jgrf_hash_crc32(void) {
    gameinfo.crc = mz_crc32(gdata.crc, gameinfo.data, gameinfo.size);
}

// Convert a nybble's hexadecimal representation to a lower case ASCII char
static inline char jgrf_nyb_hexchar(unsigned nyb) {
    nyb &= 0xf; // Lower nybble only
    if (nyb >= 10)
        nyb += 'a' - 10;
    else
        nyb += '0';
    return (char)nyb;
}

// Generate the MD5 checksum of the game data
static void jgrf_hash_md5(void) {
    MD5_CTX c;
    size_t md5len = gameinfo.size;
    uint8_t *dataptr = gameinfo.data;
    uint8_t digest[16];
    MD5_Init(&c);
    /*while (md5len > 0) { // Use for large file sizes
        md5len > 512 ? MD5_Update(&c, dataptr, 512) :
        MD5_Update(&c, dataptr, md5len);
        md5len -= 512;
        dataptr += 512;
    }*/
    MD5_Update(&c, dataptr, md5len);
    MD5_Final(digest, &c);

    // Convert the digest to a string without dodgy calls to snprintf
    for (int i = 0; i < 16; ++i) {
        gdata.md5[i * 2] = jgrf_nyb_hexchar(digest[i] >> 4);
        gdata.md5[(i * 2) + 1] = jgrf_nyb_hexchar(digest[i]);
    }
    gdata.md5[32] = '\0';

    gameinfo.md5 = gdata.md5;
}

// Set number of frames for Benchmark mode
void jgrf_benchmark(size_t frames) {
    bmarkframes = frames;
    bmark = 1;
}

// Load an Auxiliary File
void jgrf_auxfile_load(const char *filename, int index) {
    FILE *file = fopen(filename, "rb");

    if (!file)
        jgrf_log(JG_LOG_ERR, "Failed to open file: %s\n", filename);

    fseek(file, 0, SEEK_END);
    auxinfo[index].size = ftell(file);
    rewind(file);

    auxinfo[index].data = calloc(auxinfo[index].size, sizeof(uint8_t));
    if (!auxinfo[index].data ||
        !fread((void*)auxinfo[index].data, auxinfo[index].size, 1, file)) {
        jgrf_log(JG_LOG_ERR, "Failed to read file: %s\n", filename);
        fclose(file);
        return;
    }

    snprintf(gdata.auxfilepath[index], sizeof(gdata.auxfilepath[index]),
        "%s", filename);
    auxinfo[index].path = filename;

    // Close the file - some cores may want to load it again on their own terms
    fclose(file);

    snprintf(gdata.auxname[index], sizeof(gdata.auxname[index]),
        "%s", basename((char*)filename));

    // Strip the file extension off
    for (int j = strlen(gdata.auxname[index]) - 1; j > 0; --j) {
        if (gdata.auxname[index][j] == '.') {
            gdata.auxname[index][j] = '\0';
            break;
        }
    }

    auxinfo[index].name = gdata.auxname[index];
}

// Load the game data from a .zip archive
static int jgrf_game_load_archive(const char *filename) {
    // Don't load archived files if the emulator expects an archive
    if (gdata.hints & JG_HINT_MEDIA_ARCHIVED)
        return 0;

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    // Make sure it's actually a zip file
    if (!mz_zip_reader_init_file(&zip_archive, filename, 0))
        return 0;

    // Open the first ROM in the archive
    for (int i = 0; i < (int)mz_zip_reader_get_num_files(&zip_archive); ++i) {
        mz_zip_archive_file_stat file_stat;

        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            mz_zip_reader_end(&zip_archive);
            jgrf_log(JG_LOG_ERR, "Failed to stat archive. Exiting...\n");
        }

        // Extract the data into memory
        gameinfo.size = file_stat.m_uncomp_size;
        gameinfo.data = mz_zip_reader_extract_file_to_heap(&zip_archive,
            file_stat.m_filename, NULL, 0);

        // Get the CRC32 Checksum
        gameinfo.crc = gdata.crc = file_stat.m_crc32;

        // Get the MD5 Checksum
        jgrf_hash_md5();

        // Set the game name based on the file inside the zip
        jgrf_gamename(file_stat.m_filename);

        // Set the game filename to the name of the zip
        gameinfo.path = filename;
        jgapi.jg_set_gameinfo(gameinfo);

        // If the game was not loaded correctly, then fail
        if (!jgapi.jg_game_load()) {
            mz_zip_reader_end(&zip_archive);
            jgrf_log(JG_LOG_ERR,
                "Failed to load ROM from archive. Exiting...\n");
        }

        // Why don't you clean up after yourself?
        mz_zip_reader_end(&zip_archive);
        loaded.game = 1;
        return 1;
    }

    mz_zip_reader_end(&zip_archive);

    return 0;
}

static void jgrf_game_load(const char *filename) {
    // Load a game
    if (jgrf_game_load_archive(filename))
        return;

    FILE *file = fopen(filename, "rb");

    if (!file) jgrf_log(JG_LOG_ERR, "Failed to open file. Exiting...\n");

    fseek(file, 0, SEEK_END);
    gameinfo.size = ftell(file);
    rewind(file);

    gameinfo.data = calloc(gameinfo.size, sizeof(uint8_t));
    if (!gameinfo.data || !fread((void*)gameinfo.data, gameinfo.size, 1, file))
        jgrf_log(JG_LOG_ERR, "Failed to read file. Exiting...\n");

    // Close the file - some cores may want to load it again on their own terms
    fclose(file);

    // Get the CRC32 Checksum
    jgrf_hash_crc32();

    // Get the MD5 Checksum
    jgrf_hash_md5();

    jgrf_gamename(filename); // Set the game name
    gameinfo.path = filename;
    jgapi.jg_set_gameinfo(gameinfo);

    // If the game could not be loaded, there is no point continuing
    if (!jgapi.jg_game_load())
        jgrf_log(JG_LOG_ERR, "Failed to load game. Exiting...\n");

    // This item is set mostly so there can be a clean shutdown
    loaded.game = 1;

    return;
}

// Detect game type inside .zip archives
static int jgrf_game_detect_zip(const char *filename) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    // Make sure it's actually a zip file
    if (!mz_zip_reader_init_file(&zip_archive, filename, 0))
        return 0;

    // Cycle through files in the archive until one matches a known game type
    for (int i = 0; i < (int)mz_zip_reader_get_num_files(&zip_archive); ++i) {
        mz_zip_archive_file_stat file_stat;

        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            mz_zip_reader_end(&zip_archive);
            jgrf_log(JG_LOG_ERR, "Failed to stat archive. Exiting...\n");
        }

        // Set the game name based on the file inside the zip
        if (jgrf_game_detect_sys(file_stat.m_filename)) {
            mz_zip_reader_end(&zip_archive);
            return 1;
        }
    }

    mz_zip_reader_end(&zip_archive);

    return 0;
}

// Detect what console the optical disc was designed for
static int jgrf_game_detect_bincue(const char *filename) {
    uint8_t sectorbuf[2352];

    const char *mcdmagic[4] = { // Not McDonald's Magic
        "SEGADISCSYSTEM  ", "SEGABOOTDISC    ",
        "SEGADISC        ", "SEGADATADISC    ",
    };
    size_t mcdmagiclen = strlen(mcdmagic[0]); // 16

    const char *pcemagic = "PC Engine CD-ROM SYSTEM";
    size_t pcemagiclen = strlen(pcemagic); // 23

    const char *psxmagic = "  Licensed  by  ";
    size_t psxmagiclen = strlen(psxmagic); // 16

    const char *ssmagic = "SEGA SEGASATURN ";
    size_t ssmagiclen = strlen(ssmagic); // 16

    FILE *file;
    file = fopen(filename, "r");

    if (!file)
        return 0;

    // Check heuristically for each system, least complex first
    // PSX
    fseek(file, 0x24e0, SEEK_SET); // 0x24e0 is the magic offset
    if (fread(sectorbuf, 1, 16, file)) {
        if (!memcmp(sectorbuf, psxmagic, psxmagiclen)) {
            snprintf(gdata.sys, sizeof(gdata.sys), "psx");
            fclose(file);
            return 1;
        }
    }
    rewind(file);

    // Sega Saturn
    if (fread(sectorbuf, 1, 32, file)) { // Read data for both possible offsets
        if (!memcmp(sectorbuf, ssmagic, ssmagiclen) ||
            !memcmp(sectorbuf + 16, ssmagic, ssmagiclen)) {
            snprintf(gdata.sys, sizeof(gdata.sys), "ss");
            fclose(file);
            return 1;
        }
    }
    rewind(file);

    // Mega/Sega CD
    if (fread(sectorbuf, 1, 32, file)) { // Read data for both possible offsets
        for (int i = 0; i < 4; ++i) {
            if (!memcmp(sectorbuf, mcdmagic[i], mcdmagiclen) ||
                !memcmp(sectorbuf + 16, mcdmagic[i], mcdmagiclen)) {
                snprintf(gdata.sys, sizeof(gdata.sys), "md");
                fclose(file);
                return 1;
            }
        }
    }
    rewind(file);

    // PCE
    #if defined(__MINGW32__) || defined(__MINGW64__)
    // On Windows, just assume PCE, since the search using mmemmem will fail.
    snprintf(gdata.sys, sizeof(gdata.sys), "pce");
    jgrf_log(JG_LOG_WRN, "Heuristic checks failed, assuming PCE CD\n");
    #else
    // Loop through the bin, reading 2048 byte chunks at a time
    while (fread(sectorbuf, 1, 2048, file) == 2048) {
        if(mmemmem(sectorbuf, 2048, (uint8_t*)pcemagic, pcemagiclen) != NULL) {
            snprintf(gdata.sys, sizeof(gdata.sys), "pce");
            fclose(file);
            return 1;
        }
    }
    #endif

    fclose(file);

    return 0;
}

// Read a cuesheet to know how to access tracks for further heuristic checks
static int jgrf_game_detect_cue(const char *filename) {
    FILE *file;
    file = fopen(filename, "r");

    if (!file) return 0;

    // Parse cuesheet for FILE entries to read from to determine system type
    char line[256]; // Should be big enough...
    char binpath[256];
    char binfullpath[512];
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "FILE")) {
            char *binfile = strchr(line, '"');
            if (binfile) binfile++;
            else continue;

            char *end = strrchr(binfile, '"');
            if (end) *end = '\0';
            else continue;

            // If we got this far, try to read into the binary file
            snprintf(binpath, sizeof(binpath), "%s", filename);
            end = strrchr(binpath, SEP);
            if (end) *(end + 1) = '\0';
            else *binpath = '\0';
            snprintf(binfullpath, sizeof(binfullpath),
                "%s%s", binpath, binfile);

            if (jgrf_game_detect_bincue(binfullpath)) {
                fclose(file);
                return 1;
            }
        }
    }

    fclose(file);

    return 0;
}

// Read an M3U playlist to check for relevant cuesheets to be read
static int jgrf_game_detect_m3u(const char *filename) {
    FILE *file;
    file = fopen(filename, "r");

    if (!file)
        return 0;

    char line[256];
    char cuepath[384]; // Cue paths are a bit larger than their containing M3U
    char cuefullpath[640];

    while (fgets(line, sizeof(line), file)) {
        // Read .cue entries from the M3U, assuming relative paths
        snprintf(cuepath, sizeof(cuepath), "%s", filename);
        line[strlen(line) - 1] = '\0'; // Remove the newline character

        // Build the absolute path to the .cue file
        char *end = strrchr(cuepath, SEP);
        if (end) *(end + 1) = '\0';
        else *cuepath = '\0';
        snprintf(cuefullpath, sizeof(cuefullpath), "%s%s", cuepath, line);

        if (jgrf_game_detect_cue(cuefullpath)) {
            fclose(file);
            return 1;
        }
    }

    fclose(file);

    return 0;
}

// Detect the desired system based on file extension
int jgrf_game_detect_sys(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext != NULL)
        ext++;
    else
        return 0;

    // Generic file extensions
    if (!strcasecmp(ext, "bin"))
        return 0;

    else if (!strcasecmp(ext, "cue"))
        return jgrf_game_detect_cue(filename);

    else if (!strcasecmp(ext, "m3u"))
        return jgrf_game_detect_m3u(filename);

    else  if (!strcasecmp(ext, "zip"))
        return jgrf_game_detect_zip(filename);

    // Non-generic file extensions
    else if (!strcasecmp(ext, "32x"))
        snprintf(gdata.sys, sizeof(gdata.sys), "32x");

    else if (!strcasecmp(ext, "a78"))
        snprintf(gdata.sys, sizeof(gdata.sys), "7800");

    else if (!strcasecmp(ext, "col") || !strcasecmp(ext, "rom"))
        snprintf(gdata.sys, sizeof(gdata.sys), "coleco");

    else if (!strcasecmp(ext, "gb") || !strcasecmp(ext, "gbc"))
        snprintf(gdata.sys, sizeof(gdata.sys), "gb");

    else if (!strcasecmp(ext, "gba"))
        snprintf(gdata.sys, sizeof(gdata.sys), "gba");

    else if (!strcasecmp(ext, "gg"))
        snprintf(gdata.sys, sizeof(gdata.sys), "gg");

    else if (!strcasecmp(ext, "lnx"))
        snprintf(gdata.sys, sizeof(gdata.sys), "lynx");

    else if (!strcasecmp(ext, "md"))
        snprintf(gdata.sys, sizeof(gdata.sys), "md");

    else if (!strcasecmp(ext, "nds") || !strcasecmp(ext, "dsi"))
        snprintf(gdata.sys, sizeof(gdata.sys), "nds");

    else if (!strcasecmp(ext, "neo"))
        snprintf(gdata.sys, sizeof(gdata.sys), "neogeo");

    else if (!strcasecmp(ext, "nes") || !strcasecmp(ext, "fds"))
        snprintf(gdata.sys, sizeof(gdata.sys), "nes");

    else if (!strcasecmp(ext, "ngp") || !strcasecmp(ext, "ngc"))
        snprintf(gdata.sys, sizeof(gdata.sys), "ngp");

    else if (!strcasecmp(ext, "pce") || !strcasecmp(ext, "sgx"))
        snprintf(gdata.sys, sizeof(gdata.sys), "pce");

    else if (!strcasecmp(ext, "sfc") || !strcasecmp(ext, "smc") ||
        !strcasecmp(ext, "bs") || !strcasecmp(ext, "st"))
        snprintf(gdata.sys, sizeof(gdata.sys), "snes");

    else if (!strcasecmp(ext, "sg"))
        snprintf(gdata.sys, sizeof(gdata.sys), "sg");

    else if (!strcasecmp(ext, "sms"))
        snprintf(gdata.sys, sizeof(gdata.sys), "sms");

    else if (!strcasecmp(ext, "vb"))
        snprintf(gdata.sys, sizeof(gdata.sys), "vb");

    else if (!strcasecmp(ext, "vec"))
        snprintf(gdata.sys, sizeof(gdata.sys), "vectrex");

    else if (!strcasecmp(ext, "ws") || !strcasecmp(ext, "wsc"))
        snprintf(gdata.sys, sizeof(gdata.sys), "wswan");

    else
        return 0; //Failed to detect a system

    return 1;
}

#ifndef JGRF_STATIC
// Set the default core for a detected system - can be overridden at CLI
static int jgrf_core_default(void) {
    if (!strcmp(gdata.sys, "32x"))
        snprintf(gdata.corename, sizeof(gdata.corename), "picodrive");

    else if (!strcmp(gdata.sys, "7800"))
        snprintf(gdata.corename, sizeof(gdata.corename), "prosystem");

    else if (!strcmp(gdata.sys, "coleco"))
        snprintf(gdata.corename, sizeof(gdata.corename), "jollycv");

    else if (!strcmp(gdata.sys, "gb"))
        snprintf(gdata.corename, sizeof(gdata.corename), "gambatte");

    else if (!strcmp(gdata.sys, "gba"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mgba");

    else if (!strcmp(gdata.sys, "gg"))
        snprintf(gdata.corename, sizeof(gdata.corename), "cega");

    else if (!strcmp(gdata.sys, "md"))
        snprintf(gdata.corename, sizeof(gdata.corename), "genplus");

    else if (!strcmp(gdata.sys, "lynx"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");

    else if (!strcmp(gdata.sys, "nds"))
        snprintf(gdata.corename, sizeof(gdata.corename), "melonds");

    else if (!strcmp(gdata.sys, "neogeo"))
        snprintf(gdata.corename, sizeof(gdata.corename), "geolith");

    else if (!strcmp(gdata.sys, "nes"))
        snprintf(gdata.corename, sizeof(gdata.corename), "nestopia");

    else if (!strcmp(gdata.sys, "ngp"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");

    else if (!strcmp(gdata.sys, "pce"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");

    else if (!strcmp(gdata.sys, "psx"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");

    else if (!strcmp(gdata.sys, "sg"))
        snprintf(gdata.corename, sizeof(gdata.corename), "cega");

    else if (!strcmp(gdata.sys, "sms"))
        snprintf(gdata.corename, sizeof(gdata.corename), "cega");

    else if (!strcmp(gdata.sys, "snes"))
        snprintf(gdata.corename, sizeof(gdata.corename), "bsnes");

    else if (!strcmp(gdata.sys, "ss"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");

    else if (!strcmp(gdata.sys, "vb"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");

    else if (!strcmp(gdata.sys, "vectrex"))
        snprintf(gdata.corename, sizeof(gdata.corename), "vecx");

    else if (!strcmp(gdata.sys, "wswan"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");

    else
        return 0; // Failed to detect a default core

    return 1;
}
#endif

// Push data to the core
void jgrf_data_push(uint32_t type, int port, const void *ptr, size_t size) {
    jgapi.jg_data_push(type, port, ptr, size);
}

// Send a reset signal to the emulator core
void jgrf_reset(int hard) {
    jgapi.jg_reset(hard);
}

// Select disc/disk or other media to be used by the core
void jgrf_media_select(void) {
    jgapi.jg_media_select();
}

// Insert/eject media
void jgrf_media_insert(void) {
    jgapi.jg_media_insert();
}

// Rehash the emulator core's settings
void jgrf_rehash_core(void) {
    jgapi.jg_rehash();
}

// Rehash the frontend's settings
void jgrf_rehash_frontend(void) {
    jgrf_video_rehash(); // A subset of video settings allow live changes
}

// Rehash input
void jgrf_rehash_input(void) {
    // Deinitialize core input states and undefine mappings
    jgrf_input_deinit_core();
    jgrf_input_undef();

    // Query core input devices
    jgrf_input_query(jgapi.jg_get_inputinfo);
}

// Call to stop and shut down at the end of the current iteration
void jgrf_schedule_quit(void) {
    running = 0;
}

// Shut everything down, clean up, exit program
void jgrf_quit(int status) {
    if (loaded.game) jgapi.jg_game_unload();
    if (loaded.core) jgrf_core_unload();
    if (loaded.audio) jgrf_audio_deinit();
    if (loaded.video) jgrf_video_deinit();
    if (loaded.input) jgrf_input_deinit();
    if (loaded.settings) jgrf_settings_deinit();
    if (loaded.policy) jgrf_policy_deinit();
    if (gameinfo.data) free(gameinfo.data);
    for (int i = 0; i < gdata.numauxfiles; ++i)
        if (auxinfo[i].data) free(auxinfo[i].data);
    jgrf_cheats_deinit();
    SDL_Quit();
    exit(status);
}

// Retrieve the current fast-forward speed
int jgrf_get_speed(void) {
    return fforward;
}

// Set the fast-forward speed: N = extra emulation frames per screen frame
void jgrf_set_speed(int speed) {
    fforward = speed;
}

// Load state
void jgrf_state_load(int slot) {
    char statepath[260];
    snprintf(statepath, sizeof(statepath), "%s%c%s.st%d",
        gdata.statepath, SEP, gdata.gamename, slot);

    int success = jgapi.jg_state_load(statepath);

    success ? jgrf_log(JG_LOG_INF, "State Loaded: %s\n", statepath):
        jgrf_log(JG_LOG_WRN, "State Load failed: %s\n", statepath);

    jgrf_log(JG_LOG_SCR, "State %d %s",
        slot, success ? "loaded." : "load failed.");
}

void jgrf_state_load_file(const char *statepath) {
    int success = jgapi.jg_state_load(statepath);

    success ? jgrf_log(JG_LOG_INF, "State Loaded: %s\n", statepath):
        jgrf_log(JG_LOG_WRN, "State Load failed: %s\n", statepath);

    jgrf_log(JG_LOG_SCR, "State file '%s' %s",
        statepath, success ? "loaded." : "load failed.");
}

// Save state
void jgrf_state_save(int slot) {
    char statepath[260];
    snprintf(statepath, sizeof(statepath), "%s%c%s.st%d",
        gdata.statepath, SEP, gdata.gamename, slot);

    int success = jgapi.jg_state_save(statepath);

    success ? jgrf_log(JG_LOG_INF, "State Saved: %s\n", statepath):
        jgrf_log(JG_LOG_WRN, "State Save failed: %s\n", statepath);

    jgrf_log(JG_LOG_SCR, "State %d %s",
        slot, success ? "saved." : "save failed.");
}

void jgrf_set_screenfps(int fps) {
    if (!fps)
        return;

    screenfps = fps;
    jgrf_log(JG_LOG_DBG, "Screen base FPS set: %dfps\n", screenfps);
}

// Callback to inform the frontend of current core framerate
void jgrf_frametime(double frametime) {
    jgrf_audio_timing(frametime);
    corefps = frametime + 0.5;
}

static void jgrf_hooked_jg_exec_frame(void) {
    struct {
        uint32_t pixfmt;
        unsigned int w;
        unsigned int h;
        unsigned int x;
        unsigned int y;
        unsigned int nbytes;
    } frame_data = {
        vidinfo->pixfmt, vidinfo->w, vidinfo->h,
        vidinfo->x, vidinfo->y, 0
    };

    // Send the frame data to the policy server
    switch (vidinfo->pixfmt) {
        case JG_PIXFMT_XRGB8888:
        case JG_PIXFMT_XBGR8888:
            frame_data.nbytes =
                (frame_data.w + frame_data.x) * (frame_data.h + frame_data.y) * 4;
            break;
        case JG_PIXFMT_RGBX5551:
        case JG_PIXFMT_RGB565:
            frame_data.nbytes =
                (frame_data.w + frame_data.x) * (frame_data.h + frame_data.y) * 2;
            break;
        default:
            jgrf_log(JG_LOG_ERR, "Unsupported pixel format\n");
            break;
    }
    write(policy_sock, &frame_data, sizeof(frame_data));
    write(policy_sock, vidinfo->buf, frame_data.nbytes);

    // Call the core's frame execution function
    stored_jg_exec_frame();
}

void jgrf_set_policy(const char *policy) {
    int port;
    char *addr_s, *port_s;
    struct sockaddr_in addr;
    struct hostent *host;

    policy_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (policy_sock < 0) {
        jgrf_log(JG_LOG_ERR, "Failed to create socket: %s\n", strerror(errno));
    }

    addr_s = strtok(policy, ":");
    port_s = strtok(NULL, ":");
    host = gethostbyname(addr_s);
    port = atoi(port_s);
    if (!host || !port_s || port < 1 || port > 65535) {
        jgrf_log(JG_LOG_ERR, "Invalid policy address: %s\n", policy);
    }

    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, host->h_addr, host->h_length);
    addr.sin_port = htons(port);
    if (connect(policy_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        jgrf_log(JG_LOG_ERR, "Failed to connect to policy server: %s\n",
            strerror(errno));
    }
    jgrf_log(JG_LOG_INF, "Connected to policy server at %s:%d\n", addr_s, port);

    // Hook
    stored_jg_exec_frame = jgapi.jg_exec_frame;
    jgapi.jg_exec_frame = jgrf_hooked_jg_exec_frame;
    loaded.policy = 1;
}

void jgrf_policy_deinit(void) {
    if (policy_sock >= 0) {
        close(policy_sock);
        policy_sock = -1;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        jgrf_cli_usage(argv[0]);
        jgrf_quit(EXIT_SUCCESS);
    }

    // Parse command line options
    jgrf_cli_parse(argc, argv);

    // Force DirectSound audio driver on Windows
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
    putenv("SDL_AUDIODRIVER=directsound");
#endif

    // Allow joystick input when the window is not focused
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    // Keep window fullscreen if the window manager tries to iconify it
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK |
        SDL_INIT_HAPTIC) < 0) {
        jgrf_log(JG_LOG_ERR, "Failed to initialize SDL: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    // Determine the binary's path relative to the current working directory
    if (strrchr(argv[0], SEP)) {
        int lastdirpos = strrchr(argv[0], SEP) - argv[0];
        strncpy(gdata.binpath, argv[0], sizeof(gdata.binpath));
        gdata.binpath[lastdirpos] = '\0';
    }
    else {
        snprintf(gdata.binpath, sizeof(gdata.binpath), ".");
    }

#if defined(JGRF_STATIC)
    const char *binname = jg_get_coreinfo("")->name;
#else
    const char *binname = "jollygood";
#endif

    // Set up user config path
#if defined(JGRF_STATIC) && defined(_WIN32) \
    || defined(__MINGW32__) || defined(__MINGW64__)
    snprintf(gdata.configpath, sizeof(gdata.configpath), "%s%c",
        gdata.binpath, SEP);
    snprintf(gdata.datapath, sizeof(gdata.datapath), "%s%c",
        gdata.binpath, SEP);
#else
    if (getenv("XDG_CONFIG_HOME"))
        snprintf(gdata.configpath, sizeof(gdata.configpath),
            "%s%c%s%c", getenv("XDG_CONFIG_HOME"), SEP, binname, SEP);
    else
        snprintf(gdata.configpath, sizeof(gdata.configpath),
            "%s%c.config%c%s%c", getenv("HOME"), SEP, SEP, binname, SEP);

    // Set up user data path
    if (getenv("XDG_DATA_HOME"))
        snprintf(gdata.datapath, sizeof(gdata.datapath),
            "%s%c%s%c", getenv("XDG_DATA_HOME"), SEP, binname, SEP);
    else
        snprintf(gdata.datapath, sizeof(gdata.datapath),
            "%s%c.local%cshare%c%s%c",
            getenv("HOME"), SEP, SEP, SEP, binname, SEP);
#endif

    // Set up screenshot path
    snprintf(gdata.sspath, sizeof(gdata.sspath),
        "%sscreenshots%c", gdata.datapath, SEP);

    // Load settings
    loaded.settings = jgrf_settings_init();

    // Set up function pointers for video
    jgrf_video_setfuncs();

    // Detect the system required to play the game
    if (gdata.filename == NULL) {
        jgrf_cli_usage(argv[0]);
        jgrf_log(JG_LOG_ERR, "Invalid file specified. Exiting...\n");
    }
    jgrf_game_detect_sys(gdata.filename);

#ifdef JGRF_STATIC
    char corepath[192] = "Statically linked";
    snprintf(gdata.coreassets, sizeof(gdata.coreassets), "%s", gdata.binpath);
    snprintf(gdata.corename, sizeof(gdata.corename), "%s",
        jg_get_coreinfo("")->name);

#if defined(LIBDIR) && defined(DATADIR) // Check for core assets system-wide
    char coreassets[256];
    snprintf(coreassets, sizeof(coreassets),
        "%s%cjollygood%c%s", DATADIR, SEP, SEP, gdata.corename);

    struct stat fbuf;
    if (stat(coreassets, &fbuf) == 0)
        snprintf(gdata.coreassets, sizeof(gdata.coreassets), "%s", coreassets);
#endif // defined(LIBDIR) && defined(DATADIR)

#else
    // Detect the default core for the system
    if (jgrf_cli_core()) {
        snprintf(gdata.corename, sizeof(gdata.corename), "%s", jgrf_cli_core());
    }
    else if (!jgrf_core_default())
        jgrf_log(JG_LOG_ERR,
            "Cannot detect default core, or invalid file. Exiting...\n");

    // Set the core path to the local core path
    char corepath[384];
    snprintf(corepath, sizeof(corepath), "%s%ccores%c%s%c%s.%s",
        gdata.binpath, SEP, SEP, gdata.corename, SEP, gdata.corename, SOEXT);

    // Check if a core exists at that path
    struct stat fbuf;
    int corefound = 0;
    if (stat(corepath, &fbuf) == 0) {
        // Set core asset path
        snprintf(gdata.coreassets, sizeof(gdata.coreassets), "%s%ccores%c%s",
            gdata.binpath, SEP, SEP, gdata.corename);
        corefound = 1;
    }
#if defined(LIBDIR) && defined(DATADIR) // Check for the core system-wide
    else {
        snprintf(corepath, sizeof(corepath), "%s%cjollygood%c%s.%s",
            LIBDIR, SEP, SEP, gdata.corename, SOEXT);

        // If it was found, set the core assets path
        if (stat(corepath, &fbuf) == 0) {
            snprintf(gdata.coreassets, sizeof(gdata.coreassets),
                "%s%cjollygood%c%s", DATADIR, SEP, SEP, gdata.corename);
            corefound = 1;
        }
    }

    // If no core was found, there is no reason to keep the program running
    if (!corefound)
        jgrf_log(JG_LOG_ERR, "Failed to locate core. Exiting...\n");
#endif // defined(LIBDIR) && defined(DATADIR)

#endif // JGRF_STATIC

    // Load the core
    jgrf_core_load(corepath);

    // Override any core specific settings
    jgrf_settings_override(gdata.corename);

    // Do final overrides using command line options
    jgrf_cli_override();

    // Create any directories that are required
    jgrf_mkdirs();

    // Load Auxiliary files
    for (int i = 0; i < gdata.numauxfiles; ++i)
        jgapi.jg_set_auxinfo(auxinfo[i], i);

    // Set hints from the core
    gdata.hints = coreinfo->hints;

    // Load the game
    jgrf_game_load(gdata.filename);

    // Update hints in case loading a game caused a change
    gdata.hints = coreinfo->hints;

    // Set up function pointers for video
    jgrf_video_setfuncs();

    // Show core information
    jgrf_log(JG_LOG_INF, "Core: %s (%s %s)\n",
        gdata.corename, gdata.corefname, gdata.coreversion);
    jgrf_log(JG_LOG_DBG, "Core System: %s\n", gdata.sys);
    jgrf_log(JG_LOG_DBG, "Core Path: %s\n", corepath);
    jgrf_log(JG_LOG_DBG, "Core Asset Path: %s\n", gdata.coreassets);

    // Set up video in the frontend and the core
    loaded.video = jgrf_video_init();
    jgapi.jg_setup_video();

    // Create the window
    jgrf_video_create();

    // Initialize audio output
    loaded.audio = jgrf_audio_init();
    jgapi.jg_setup_audio();

    // Initialize input
    loaded.input = jgrf_input_init();

    // Query core input devices
    jgrf_input_query(jgapi.jg_get_inputinfo);

    // Reset the core
    jgapi.jg_reset(2);

    // Activate Cheats
    jgrf_cheats_init(jgapi.jg_cheat_clear, jgapi.jg_cheat_set);

    // Allow the sound to flow
    jgrf_audio_unpause();

    // Explicitly disable the screensaver
    SDL_DisableScreenSaver();

    // SDL_Event struct to be filled and passed to SDL event queue
    SDL_Event event;

    // Load saved game state
    if (jgrf_cli_state()) {
        jgrf_state_load_file(jgrf_cli_state());
    }

    // Use defined policy
    if (jgrf_cli_policy()) {
        jgrf_set_policy(jgrf_cli_policy());
    }

    int runframes = 0;
    double collector = 0;

    while (running) {
        // Divide the core framerate by the base framerate
        runframes = (corefps / screenfps); // Screenfps is monitor refresh

        // Collect the remainder of the same division operation
        collector += ((double)corefps / screenfps) - runframes;

        // When sufficient remainder has been collected, run an extra frame
        if (collector >= 1.0) {
            ++runframes;
            collector -= 1.0;
        }

        // Run the required number of emulator iterations (frames)
        // Fast-forward works by running extra frames and downsampling audio
        for (int i = 0; i < runframes + fforward; ++i)
            jgapi.jg_exec_frame();

        framecount += (runframes + fforward);

        // Render and output the current video
        jgrf_video_render(runframes);
        jgrf_video_swapbuffers();

        if (bmark && framecount >= bmarkframes) {
            jgrf_log(JG_LOG_INF, "Benchmark completed after %ld frames\n",
                bmarkframes);
            jgrf_quit(EXIT_SUCCESS);
        }

        // Poll for events
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: {
                    running = 0;
                    break;
                }
                case SDL_WINDOWEVENT: {
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_MOVED:
                        case SDL_WINDOWEVENT_RESIZED: {
                            jgrf_video_resize();
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                    break;
                }
                default: {
                    jgrf_input_handler(&event);
                    break;
                }
            }
        }
    }

    // Clean up before exiting
    jgrf_quit(EXIT_SUCCESS);

    return 0;
}
