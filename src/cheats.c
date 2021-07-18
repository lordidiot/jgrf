/*
 * Copyright (c) 2021 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <jg/jg.h>

#include "tiny-json.h"

#include "jgrf.h"

#define MAX_FIELDS 4096

static void (*jgrf_cheat_clear)(void);
static void (*jgrf_cheat_set)(const char *);

static char *chtfile = NULL;

static json_t pool[MAX_FIELDS];

void jgrf_cheats_activate(void) {
    const json_t *parent = json_create(chtfile, pool, MAX_FIELDS);
    
    if (parent == NULL) {
        jgrf_log(JG_LOG_WRN, "Failed to parse cheat file\n");
        return;
    }
    
    const json_t *cheatarray = json_getProperty(parent, "cheats");
    const json_t *cheat = json_getChild(cheatarray);
    
    while (cheat) {
        const json_t *enabled = json_getProperty(cheat, "enabled");
        
        if (json_getBoolean(enabled)) { // if it's enabled
            const json_t *desc = json_getProperty(cheat, "desc");
            jgrf_log(JG_LOG_DBG, "Cheat enabled: %s\n", json_getValue(desc));
            
            const json_t *codearray = json_getProperty(cheat, "codes");
            const json_t *code = json_getChild(codearray);
            
            while (code) {
                jgrf_cheat_set(json_getValue(code));
                code = json_getSibling(code);
            }
        }
        
        cheat = json_getSibling(cheat);
    }
}

void jgrf_cheats_deactivate(void) {
    jgrf_cheat_clear();
}

void jgrf_cheats_deinit(void) {
    if (chtfile) free(chtfile);
}

void jgrf_cheats_init(void (*chtclear)(void), void (*chtset)(const char *)) {
    // Set the function pointers
    jgrf_cheat_clear = chtclear;
    jgrf_cheat_set = chtset;
    
    // Grab the pointer to global data
    jgrf_gdata_t *gdata = jgrf_gdata_ptr();
    
    // Set the cheat file path
    char chtfilepath[264];
    snprintf(chtfilepath, sizeof(chtfilepath), "%s/%s.json",
        gdata->cheatpath, gdata->gamename);
    
    // Attempt to open the file
    FILE *file = fopen(chtfilepath, "rb");
    
    if (!file) {
        jgrf_log(JG_LOG_DBG, "No cheat file: %s\n", chtfilepath);
        return;
    }
    
    fseek(file, 0, SEEK_END);
    size_t chtfilesize = ftell(file) * sizeof(char);
    rewind(file);
    
    chtfile = (char*)malloc(chtfilesize);
    
    if (!chtfile || !fread((void*)chtfile, chtfilesize, 1, file)) {
        jgrf_log(JG_LOG_WRN, "Failed to read file: %s\n", chtfilepath);
        return;
    }
    
    jgrf_log(JG_LOG_DBG, "Cheat file loaded: %s\n", chtfilepath);
    
    // Activate any cheats
    jgrf_cheats_activate();
}
