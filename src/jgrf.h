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

#ifndef MAIN_H
#define MAIN_H

#define VERSION "1.0.2"

#define JGRF_AUXFILE_MAX 3

typedef struct jgrf_gdata_t { // Global Data
    const char *filename;
    char configpath[128]; // Base user config path
    char datapath[64]; // Base user data path
    char corename[64]; // Internally used core name
    char corefname[96]; // Core Full Name
    char coreversion[32]; // Core Version
    char gamename[128]; // Internally used game name
    char gamefname[128]; // Internally used game name with extension
    char coreassets[128]; // Core asset path
    char userassets[128]; // User asset path
    char biospath[128]; // BIOS path
    char cheatpath[128]; // Cheat path
    char statepath[128]; // State path
    char savepath[128]; // Save path
    char sspath[128]; // Screenshot path
    char auxfilepath[JGRF_AUXFILE_MAX][128]; // Auxiliary file paths
    char auxname[JGRF_AUXFILE_MAX][128]; // Auxiliary file names minus path/ext
    uint32_t crc; // CRC32 Checksum
    char md5[33]; // MD5 Checksum
    char sys[24]; // Name of emulated system
    int numinputs; // Number of ports/inputs in core
    int numauxfiles; // Number of loaded support files
    uint32_t hints; // Hints from the core
} jgrf_gdata_t;

jgrf_gdata_t *jgrf_gdata_ptr(void);

void jgrf_log(int, const char*, ...);

void jgrf_auxfile_load(const char*, int);

void jgrf_benchmark(size_t);

void jgrf_data_push(uint32_t, int, const void*, size_t);

void jgrf_state_load(int);
void jgrf_state_save(int);

void jgrf_reset(int);

void jgrf_media_select(void);
void jgrf_media_insert(void);

void jgrf_rehash_core(void);
void jgrf_rehash_frontend(void);
void jgrf_rehash_input(void);

void jgrf_schedule_quit(void);
void jgrf_quit(int);

int jgrf_get_speed(void);
void jgrf_set_speed(int);
void jgrf_set_screenfps(int);

void jgrf_state_save(int);
void jgrf_state_load(int);

int jgrf_game_detect_sys(const char*);

void jgrf_frametime(double);

extern int bmark; // External benchmark mode variable
extern int fforward; // External fast-forward level
extern int corefps; // Emulator core's framerate rounded to nearest int
extern int screenfps; // Screen's framerate rounted to nearest int

#endif
