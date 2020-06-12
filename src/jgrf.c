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
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>

#include <SDL.h>
#include <jg/jg.h>

#include "md5.h"
#include "miniz.h"
#include "musl_memmem.h"

#include "jgrf.h"
#include "audio.h"
#include "cli.h"
#include "input.h"
#include "settings.h"
#include "video.h"

#ifdef __APPLE__
    #define SOEXT "dylib"
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define SOEXT "dll"
#else
    #define SOEXT "so"
#endif

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
    int (*jg_state_load)(const char *);
    int (*jg_state_save)(const char *);
    void (*jg_media_select)(void);
    void (*jg_media_insert)(void);
    // Callback Setup
    void (*jg_set_cb_log)(jg_cb_log_t);
    void (*jg_set_cb_audio)(jg_cb_audio_t);
    void (*jg_set_cb_frametime)(jg_cb_frametime_t);
    void (*jg_set_cb_rumble)(jg_cb_rumble_t);
    void (*jg_set_cb_settings_read)(jg_cb_settings_read_t);
    // Retrieve "info" structs from the core
    jg_coreinfo_t* (*jg_get_coreinfo)(const char *);
    jg_videoinfo_t* (*jg_get_videoinfo)(void);
    jg_audioinfo_t* (*jg_get_audioinfo)(void);
    jg_inputinfo_t* (*jg_get_inputinfo)(int);
    // Core setup
    void (*jg_setup_video)(void);
    void (*jg_setup_audio)(void);
    void (*jg_set_inputstate)(jg_inputstate_t*, int);
    void (*jg_set_gameinfo)(jg_gameinfo_t);
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
} loaded = { 0, 0, 0, 0, 0, 0 };

// Frontend knows the game and path info and passes this to the core
static jg_gameinfo_t gameinfo;
static jg_pathinfo_t pathinfo;

// Global data struct for miscellaneous information
static jgrf_gdata_t gdata;
jgrf_gdata_t *jgrf_gdata_ptr() {
    return &gdata;
}

// Program should keep running (1) or, or shut down (0)
static int running = 1;

// Number of extra frames to run for fast-forwarding purposes
int fforward = 0;

// Frame timing
static int corefps = 60;
static int basefps = 60;
static double screenfps = 60.0;
static uint32_t t0 = 0;
static uint32_t framecount = 0;

// Recursive mkdir (similar to mkdir -p)
static void mkdirr(const char *dir) {
    char tmp[256];
    char *p = NULL;
    
    size_t len = snprintf(tmp, sizeof(tmp), "%s", dir);
    
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            #if defined(__MINGW32__) || defined(__MINGW64__)
            mkdir(tmp);
            #else
            mkdir(tmp, S_IRWXU);
            #endif
            *p = '/';
        }
    }
    #if defined(__MINGW32__) || defined(__MINGW64__)
    mkdir(tmp);
    #else
    mkdir(tmp, S_IRWXU);
    #endif
}

// Create user directories
static void jgrf_mkdirs() {
    mkdirr(gdata.configpath);
    mkdirr(gdata.datapath);
    mkdirr(gdata.biospath);
    mkdirr(gdata.userassets);
    mkdirr(gdata.savepath);
    mkdirr(gdata.statepath);
    mkdirr(gdata.sspath);
}

// Tell the emulator core about the paths the frontend knows
static void jgrf_set_paths() {
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
    for (int i = strlen(gdata.gamename) - 1; i > 0; i--) {
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
        "\033[0;36m", "\033[0m", "\033[0;33m", "\033[1;31m"
    };
    
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    va_end(va);
    
    FILE *fout = level == 1 ? stdout : stderr;
    
    settings_t *settings = jgrf_get_settings();
    if (level >= settings->misc_frontendlog.val) {
        fprintf(fout, "%s%c: %s\033[0m", lcol[level], lchr[level], buffer);
        fflush(fout);
    }
    
    if (level == JG_LOG_ERR)
        jgrf_quit(EXIT_FAILURE);
}

// Handle log output from the core
void jgrf_core_log(int level, const char *fmt, ...) {
    va_list va;
    char buffer[512];
    static const char *lchr = "DIWE";
    static const char *lcol[4] = {
        "\033[0;35m", "\033[0;34m", "\033[7;33m", "\033[1;7;31m"
    };
    
    va_start(va, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, va);
    va_end(va);
    
    FILE *fout = level == 1 ? stdout : stderr;
    
    settings_t *settings = jgrf_get_settings();
    if (level == JG_LOG_SCR) {
        jgrf_video_text(1, corefps, buffer);
    }
    else if (level >= settings->misc_corelog.val) {
        fprintf(fout, "%s%c: %s\033[0m", lcol[level], lchr[level], buffer);
        fflush(fout);
    }
    
    if (level == JG_LOG_ERR)
        jgrf_quit(EXIT_FAILURE);
}

// Pass callbacks into the core
static void jgrf_callbacks_set() {
    jgapi.jg_set_cb_log(&jgrf_core_log);
    jgapi.jg_set_cb_audio(&jgrf_audio_cb_core);
    jgapi.jg_set_cb_frametime(&jgrf_frametime);
    jgapi.jg_set_cb_rumble(&jgrf_input_rumble);
    jgapi.jg_set_cb_settings_read(&jgrf_settings_emu);
}

// Load and set up the core
static void jgrf_core_load(const char *corepath) {
    memset(&jgapi, 0, sizeof(jgapi));
    jgapi.handle = dlopen(corepath, RTLD_LAZY);

    if (!jgapi.handle) jgrf_log(JG_LOG_ERR, "%s\n", dlerror());
    
    dlerror();
    
    *(void**)(&jgapi.jg_init) = dlsym(jgapi.handle, "jg_init");
    *(void**)(&jgapi.jg_deinit) = dlsym(jgapi.handle, "jg_deinit");
    *(void**)(&jgapi.jg_reset) = dlsym(jgapi.handle, "jg_reset");
    *(void**)(&jgapi.jg_exec_frame) = dlsym(jgapi.handle, "jg_exec_frame");
    *(void**)(&jgapi.jg_game_load) = dlsym(jgapi.handle, "jg_game_load");
    *(void**)(&jgapi.jg_game_unload) = dlsym(jgapi.handle, "jg_game_unload");
    *(void**)(&jgapi.jg_state_load) = dlsym(jgapi.handle, "jg_state_load");
    *(void**)(&jgapi.jg_state_save) = dlsym(jgapi.handle, "jg_state_save");
    *(void**)(&jgapi.jg_media_select) = dlsym(jgapi.handle, "jg_media_select");
    *(void**)(&jgapi.jg_media_insert) = dlsym(jgapi.handle, "jg_media_insert");
    
    *(void**)(&jgapi.jg_get_coreinfo) = dlsym(jgapi.handle,
        "jg_get_coreinfo");
    *(void**)(&jgapi.jg_get_audioinfo) = dlsym(jgapi.handle,
        "jg_get_audioinfo");
    *(void**)(&jgapi.jg_get_videoinfo) = dlsym(jgapi.handle,
        "jg_get_videoinfo");
    *(void**)(&jgapi.jg_get_inputinfo) = dlsym(jgapi.handle,
        "jg_get_inputinfo");
    
    *(void**)(&jgapi.jg_setup_video) = dlsym(jgapi.handle,
        "jg_setup_video");
    *(void**)(&jgapi.jg_setup_audio) = dlsym(jgapi.handle,
        "jg_setup_audio");
    *(void**)(&jgapi.jg_set_inputstate) = dlsym(jgapi.handle,
        "jg_set_inputstate");
    *(void**)(&jgapi.jg_set_gameinfo) = dlsym(jgapi.handle,
        "jg_set_gameinfo");
    *(void**)(&jgapi.jg_set_paths) = dlsym(jgapi.handle,
        "jg_set_paths");
    
    *(void**)(&jgapi.jg_set_cb_log) = dlsym(jgapi.handle,
        "jg_set_cb_log");
    *(void**)(&jgapi.jg_set_cb_audio) = dlsym(jgapi.handle,
        "jg_set_cb_audio");
    *(void**)(&jgapi.jg_set_cb_frametime) = dlsym(jgapi.handle,
        "jg_set_cb_frametime");
    *(void**)(&jgapi.jg_set_cb_rumble) = dlsym(jgapi.handle,
        "jg_set_cb_rumble");
    *(void**)(&jgapi.jg_set_cb_settings_read) = dlsym(jgapi.handle,
        "jg_set_cb_settings_read");
    
    // Set up values in global data struct
    jg_coreinfo_t *coreinfo = jgapi.jg_get_coreinfo(gdata.sys);
    
    // Set hints from the core
    gdata.hints = coreinfo->hints;
    
    // Match gdata.sys with coreinfo->sys in case of user overriding core at CLI
    snprintf(gdata.sys, sizeof(gdata.sys), "%s", coreinfo->sys);
    
    // Populate frontend's global data with information from the core
    snprintf(gdata.corefname, sizeof(gdata.corefname),
        "%s", coreinfo->fname);
    snprintf(gdata.userassets, sizeof(gdata.userassets),
        "%sassets/%s", gdata.datapath, coreinfo->name); 
    snprintf(gdata.biospath, sizeof(gdata.biospath),
        "%sbios", gdata.datapath);
    snprintf(gdata.savepath, sizeof(gdata.savepath),
        "%ssave/%s", gdata.datapath, coreinfo->name);
    snprintf(gdata.statepath, sizeof(gdata.statepath),
        "%sstate/%s", gdata.datapath, coreinfo->name);
    gdata.numinputs = coreinfo->numinputs;
    
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
    jgrf_video_set_info(jgapi.jg_get_videoinfo());
    jgrf_audio_set_info(jgapi.jg_get_audioinfo());
    
    // Hand pointers to input states to the core
    jgrf_input_set_states(jgapi.jg_set_inputstate);
    
    // Initialize the emulator core
    loaded.core = jgapi.jg_init();
    if (!loaded.core) {
        jgrf_log(JG_LOG_ERR, "Failed to initialize core, exiting...\n");
        jgrf_quit(EXIT_FAILURE);
    }
}

// Unload the core
static void jgrf_core_unload() {
    jgapi.jg_deinit();
    if (jgapi.handle) { dlclose(jgapi.handle); }
}

// Generate the CRC32 checksum of the game data
static void jgrf_hash_crc32() {
    gameinfo.crc = mz_crc32(gdata.crc, gameinfo.data, gameinfo.size);
}

// Generate the MD5 checksum of the game data
static void jgrf_hash_md5() {
    MD5_CTX c;
    size_t md5len = gameinfo.size;
    uint8_t *dataptr = gameinfo.data;
    unsigned char digest[16];
    MD5_Init(&c);
    /*while (md5len > 0) { // Use for large file sizes
        md5len > 512 ? MD5_Update(&c, dataptr, 512) :
        MD5_Update(&c, dataptr, md5len);
        md5len -= 512;
        dataptr += 512;
    }*/
    MD5_Update(&c, dataptr, md5len);
    MD5_Final(digest, &c);
    
    for (int i = 0; i < 16; i++) {
        snprintf(&(gdata.md5[i * 2]), 16 * 2, "%02x", (unsigned)digest[i]);
    }
    gameinfo.md5 = gdata.md5;
}

// Load the game data from a .zip archive
static int jgrf_game_load_archive(const char *filename) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));
    
    // Make sure it's actually a zip file
    if (!mz_zip_reader_init_file(&zip_archive, filename, 0))
        return 0;
    
    // Open the first ROM in the archive
    for (int i = 0; i < (int)mz_zip_reader_get_num_files(&zip_archive); i++) {
        mz_zip_archive_file_stat file_stat;
        
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            mz_zip_reader_end(&zip_archive);
            jgrf_log(JG_LOG_ERR, "Failed to stat archive, exiting...\n");
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
                "Failed to load ROM from archive, exiting...\n");
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
    
    if (!file) jgrf_log(JG_LOG_ERR, "Failed to open file, exiting...\n");
    
    fseek(file, 0, SEEK_END);
    gameinfo.size = ftell(file);
    rewind(file);
    
    gameinfo.data = malloc(gameinfo.size);
    if (!gameinfo.data || !fread((void*)gameinfo.data, gameinfo.size, 1, file))
        jgrf_log(JG_LOG_ERR, "Failed to read file, exiting...\n");
    
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
        jgrf_log(JG_LOG_ERR, "Failed to load game, exiting...\n");
    
    // This item is set mostly so there can be a clean shutdown
    loaded.game = 1;
    return;
}

// Detect game type inside .zip archives
int jgrf_game_detect_zip(const char *filename) {
    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));
    
    // Make sure it's actually a zip file
    if (!mz_zip_reader_init_file(&zip_archive, filename, 0))
        return 0;
    
    // Cycle through files in the archive until one matches a known game type
    for (int i = 0; i < (int)mz_zip_reader_get_num_files(&zip_archive); i++) {
        mz_zip_archive_file_stat file_stat;
        
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            mz_zip_reader_end(&zip_archive);
            jgrf_log(JG_LOG_ERR, "Failed to stat archive, exiting...\n");
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
    
    if (!file) return 0;
    
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
        for (int i = 0; i < 4; i++) {
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
            end = strrchr(binpath, '/');
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
    
    if (!file) return 0;
    
    char line[256];
    char cuepath[384]; // Cue paths are a bit larger than their containing M3U
    char cuefullpath[640];
    
    while (fgets(line, sizeof(line), file)) {
        // Read .cue entries from the M3U, assuming relative paths
        snprintf(cuepath, sizeof(cuepath), "%s", filename);
        line[strlen(line) - 1] = '\0'; // Remove the newline character
        
        // Build the absolute path to the .cue file
        char *end = strrchr(cuepath, '/');
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
        jgrf_log(JG_LOG_ERR, "File not found: %s\n", filename);
    
    if (!strcasecmp(ext, "bin"))
        return 0;
    
    else if (!strcasecmp(ext, "cue"))
        return jgrf_game_detect_cue(filename);
    
    else if (!strcasecmp(ext, "m3u"))
        return jgrf_game_detect_m3u(filename);
    
    else  if (!strcasecmp(ext, "zip"))
        return jgrf_game_detect_zip(filename);
    
    else if (!strcasecmp(ext, "nes") || !strcasecmp(ext, "fds"))
        snprintf(gdata.sys, sizeof(gdata.sys), "nes");
    
    else if (!strcasecmp(ext, "gb") || !strcasecmp(ext, "gbc"))
        snprintf(gdata.sys, sizeof(gdata.sys), "gb");
    
    else if (!strcasecmp(ext, "gba"))
        snprintf(gdata.sys, sizeof(gdata.sys), "gba");
        
    else if (!strcasecmp(ext, "sfc") || !strcasecmp(ext, "smc") ||
        !strcasecmp(ext, "bs"))
        snprintf(gdata.sys, sizeof(gdata.sys), "snes");
    
    else if (!strcasecmp(ext, "md") || !strcasecmp(ext, "sms") ||
        !strcasecmp(ext, "gg") || !strcasecmp(ext, "sg"))
        snprintf(gdata.sys, sizeof(gdata.sys), "md"); // genplus
    
    else if (!strcasecmp(ext, "32x"))
        snprintf(gdata.sys, sizeof(gdata.sys), "32x");
    
    else if (!strcasecmp(ext, "a78"))
        snprintf(gdata.sys, sizeof(gdata.sys), "7800");
    
    else if (!strcasecmp(ext, "lnx"))
        snprintf(gdata.sys, sizeof(gdata.sys), "lynx");
    
    else if (!strcasecmp(ext, "ngp") || !strcasecmp(ext, "ngc"))
        snprintf(gdata.sys, sizeof(gdata.sys), "ngp");
    
    else if (!strcasecmp(ext, "pce") || !strcasecmp(ext, "sgx"))
        snprintf(gdata.sys, sizeof(gdata.sys), "pce");
    
    else if (!strcasecmp(ext, "vb"))
        snprintf(gdata.sys, sizeof(gdata.sys), "vb");
    
    else if (!strcasecmp(ext, "ws") || !strcasecmp(ext, "wsc"))
        snprintf(gdata.sys, sizeof(gdata.sys), "wswan");
    
    else
        return 0; //Failed to detect a system
    
    return 1;
}

// Set the default core for a detected system - can be overridden at CLI
static int jgrf_core_default() {
    if (!strcmp(gdata.sys, "nes"))
        snprintf(gdata.corename, sizeof(gdata.corename), "nestopia");
    
    else if (!strcmp(gdata.sys, "gb"))
        snprintf(gdata.corename, sizeof(gdata.corename), "gambatte");
    
    else if (!strcmp(gdata.sys, "gba"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mgba");
    
    else if (!strcmp(gdata.sys, "snes"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mesens");
    
    else if (!strcmp(gdata.sys, "md"))
        snprintf(gdata.corename, sizeof(gdata.corename), "genplus");
    
    else if (!strcmp(gdata.sys, "lynx"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");
    
    else if (!strcmp(gdata.sys, "ngp"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");
        
    else if (!strcmp(gdata.sys, "pce"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");
    
    else if (!strcmp(gdata.sys, "psx"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");
    
    else if (!strcmp(gdata.sys, "ss"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");
    
    else if (!strcmp(gdata.sys, "vb"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");
    
    else if (!strcmp(gdata.sys, "wswan"))
        snprintf(gdata.corename, sizeof(gdata.corename), "mednafen");
    
    else if (!strcmp(gdata.sys, "7800"))
        snprintf(gdata.corename, sizeof(gdata.corename), "prosystem");
    
    else if (!strcmp(gdata.sys, "32x"))
        snprintf(gdata.corename, sizeof(gdata.corename), "picodrive");
    
    else
        return 0; // Failed to detect a default core
    
    return 1;
}

// Send a reset signal to the emulator core
void jgrf_reset(int hard) {
    jgapi.jg_reset(hard);
}

// Select disc/disk or other media to be used by the core
void jgrf_media_select() {
    jgapi.jg_media_select();
}

// Insert/eject media
void jgrf_media_insert() {
    jgapi.jg_media_insert();
}

// Call to stop and shut down at the end of the current iteration
void jgrf_schedule_quit() {
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
    if (gameinfo.data) free(gameinfo.data);
    SDL_Quit();
    exit(status);
}

// Retrieve the current fast-forward speed
int jgrf_get_speed() {
    return fforward;
}

// Set the fast-forward speed: N = extra emulation frames per screen frame
void jgrf_set_speed(int speed) {
    fforward = speed;
}

// Load state
void jgrf_state_load(int slot) {
    char statepath[260];
    snprintf(statepath, sizeof(statepath), "%s/%s.st%d",
        gdata.statepath, gdata.gamename, slot);
    
    int success = jgapi.jg_state_load(statepath);
    
    success ? jgrf_log(JG_LOG_INF, "State Loaded: %s\n", statepath):
        jgrf_log(JG_LOG_WRN, "State Load failed: %s\n", statepath);
    
    char msg[32];
    snprintf(msg, sizeof(msg), "State %d %s",
        slot, success ? "loaded." : "load failed.");
    
    jgrf_video_text(0, 45, msg);
}

// Save state
void jgrf_state_save(int slot) {
    char statepath[260];
    snprintf(statepath, sizeof(statepath), "%s/%s.st%d",
        gdata.statepath, gdata.gamename, slot);
    
    int success = jgapi.jg_state_save(statepath);
    
    success ? jgrf_log(JG_LOG_INF, "State Saved: %s\n", statepath):
        jgrf_log(JG_LOG_WRN, "State Save failed: %s\n", statepath);
    
    char msg[32];
    snprintf(msg, sizeof(msg), "State %d %s",
        slot, success ? "saved." : "save failed.");
    
    jgrf_video_text(0, 45, msg);
}

// Callback to inform the frontend of current core framerate
void jgrf_frametime(double frametime) {
    jgrf_audio_timing(frametime);
    corefps = frametime + 0.5;
}

int main(int argc, char *argv[]) {
    if (argc < 2)
        jgrf_cli_usage();
    
    // Parse command line options
    jgrf_cli_parse(argc, argv);
    
    // Force DirectSound audio driver on Windows
    #if defined(__MINGW32__) || defined(__MINGW64__)
    putenv("SDL_AUDIODRIVER=directsound");
    #endif
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK |
        SDL_INIT_HAPTIC) < 0) {
        jgrf_log(JG_LOG_ERR, "Failed to initialize SDL: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    // Set up user config path
    if (getenv("XDG_CONFIG_HOME"))
        snprintf(gdata.configpath, sizeof(gdata.configpath),
            "%s/jollygood/", getenv("XDG_CONFIG_HOME"));
    else
        snprintf(gdata.configpath, sizeof(gdata.configpath),
            "%s/.config/jollygood/", getenv("HOME"));
    
    // Set up user data path
    if (getenv("XDG_DATA_HOME"))
        snprintf(gdata.datapath, sizeof(gdata.datapath),
            "%s/jollygood/", getenv("XDG_DATA_HOME"));
    else
        snprintf(gdata.datapath, sizeof(gdata.datapath),
            "%s/.local/share/jollygood/", getenv("HOME"));
    
    // Set up screenshot path
    snprintf(gdata.sspath, sizeof(gdata.sspath),
        "%sscreenshots/", gdata.datapath);
    
    // Load settings
    loaded.settings = jgrf_settings_init();
    
    // Set up function pointers for video
    jgrf_video_setfuncs();
    
    // Detect the system required to play the game
    gdata.filename = argv[argc - 1]; // Filename is always the last arg
    jgrf_game_detect_sys(gdata.filename);
    
    // Detect the default core for the system
    if (jgrf_cli_core()) {
        snprintf(gdata.corename, sizeof(gdata.corename), "%s", jgrf_cli_core());
    }
    else if (!jgrf_core_default())
        jgrf_log(JG_LOG_ERR,
            "Invalid file, cannot detect default core, exiting...\n");
    
    // Set the core path to the local core path
    char corepath[192];
    snprintf(corepath, sizeof(corepath), "./cores/%s/%s.%s",
        gdata.corename, gdata.corename, SOEXT);
    
    // Check if a core exists at that path
    struct stat fbuf;
    int corefound = 0;
    if (stat(corepath, &fbuf) == 0) {
        // Set core asset path
        snprintf(gdata.coreassets, sizeof(gdata.coreassets), "./cores/%s",
            gdata.corename);
        corefound = 1;
    }
#if defined(LIBDIR) && defined(DATADIR) // Check for the core system-wide
    else {
        snprintf(corepath, sizeof(corepath), "%s/%s.%s",
            LIBDIR, gdata.corename, SOEXT);
        
        // If it was found, set the core assets path
        if (stat(corepath, &fbuf) == 0) {
            snprintf(gdata.coreassets, sizeof(gdata.coreassets), "%s/%s",
                DATADIR, gdata.corename);
            corefound = 1;
        }
    }
#endif
    
    // If no core was found, there is no reason to keep the program running
    if (!corefound)
        jgrf_log(JG_LOG_ERR, "Failed to locate core, exiting...\n");
    
    // Load the core
    jgrf_core_load(corepath);
    jgrf_log(JG_LOG_DBG, "Core: %s\n", gdata.corename);
    
    // Create any directories that are required
    jgrf_mkdirs();
    
    // Load the game
    jgrf_game_load(gdata.filename);
    
    // Override any core specific settings
    jgrf_settings_override(gdata.corename);
    
    // Do final overrides using command line options
    jgrf_cli_override();
    
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
    jgapi.jg_reset(1);
    
    // Allow the sound to flow
    jgrf_audio_unpause();
    
    // Explicitly disable the screensaver
    SDL_DisableScreenSaver();
    
    // SDL_Event struct to be filled and passed to SDL event queue
    SDL_Event event;
    
    int runframes = 0;
    int collector = 0;
    
    while (running) {
        // Divide the core framerate by the base framerate
        runframes = (corefps / basefps); // Ideally, basefps is monitor refresh
        
        // Collect the remainder of the same division operation
        collector += corefps % basefps;
        
        // When sufficient remainder has been collected, run an extra frame
        // If corefps is smaller than basefps, this is the only way frames run
        if (collector >= basefps) {
            runframes++;
            collector -= basefps;
        }
        
        // Run the required number of emulator iterations (frames)
        // Fast-forward works by running extra frames and downsampling audio
        for (int i = 0; i < runframes + fforward; i++)
            jgapi.jg_exec_frame();
        
        // Render and output the current video
        jgrf_video_render();
        jgrf_video_swapbuffers();
        
        // Count frames per second (screen frames)
        framecount++;
        uint32_t t = SDL_GetTicks();
        if (t - t0 >= 1000) {
            double seconds = (t - t0) / 1000.0;
            screenfps = framecount / seconds;
            //printf("%d frames in %f seconds = %f FPS (%d)\n",
            //  framecount, seconds, screenfps, (int)(screenfps + 0.5));
            if (screenfps > 70)
                basefps = (int)(screenfps + 0.5);
            t0 = t;
            framecount = 0;
        }
        
        // Poll for events
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = 0;
                    break;
                case SDL_WINDOWEVENT:
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_MOVED:
                        case SDL_WINDOWEVENT_RESIZED:
                            jgrf_video_resize(); break;
                        default: break;
                    }
                    break;
                default:
                    jgrf_input_handler(&event);
                    break;
            }
        }
    }
    
    // Clean up before exiting
    jgrf_quit(EXIT_SUCCESS);
    return 0;
}
