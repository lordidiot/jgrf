/*
 * Copyright (c) 2020 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef MAIN_H
#define MAIN_H

typedef struct jgrf_gdata_t { // Global Data
    const char *filename;
    char configpath[128]; // Base user config path
    char datapath[64]; // Base user data path
    char corename[64]; // Internally used core name
    char corefname[96]; // Core Full Name
    char gamename[128]; // Internally used game name
    char gamefname[128]; // Internally used game name with extension
    char coreassets[128]; // Core asset path
    char userassets[128]; // User asset path
    char biospath[128]; // BIOS path
    char statepath[128]; // State path
    char savepath[128]; // Save path
    char sspath[128]; // Screenshot path
    uint32_t crc; // CRC32 Checksum
    char md5[33]; // MD5 Checksum
    char sys[24]; // Name of emulated system
    int numinputs; // Number of ports/inputs in core
    uint32_t hints; // Hints from the core
} jgrf_gdata_t;

jgrf_gdata_t *jgrf_gdata_ptr();

void jgrf_log(int, const char*, ...);

void jgrf_state_load(int);
void jgrf_state_save(int);

void jgrf_reset(int);

void jgrf_media_select();
void jgrf_media_insert();

void jgrf_schedule_quit();
void jgrf_quit(int);

int jgrf_get_speed();
void jgrf_set_speed(int);

void jgrf_state_save(int);
void jgrf_state_load(int);

int jgrf_game_detect_sys(const char*);

void jgrf_frametime(double);

#endif
