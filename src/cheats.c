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

#include "parson.h"

#include "jgrf.h"

static void (*jgrf_cheat_clear)(void);
static void (*jgrf_cheat_set)(const char *);

static char *chtfile = NULL;
static int chtactive = 0;
static int chtloaded = 0;

// Parse the cheat file and activate any cheats
void jgrf_cheats_activate(void) {
    // If cheats are not loaded, simply return
    if (!chtloaded) {
        jgrf_log(JG_LOG_WRN, "No cheats to activate\n");
        return;
    }
    else if (chtactive) {
        return;
    }
    
    jgrf_cheat_clear(); // Disable any active cheats first
    
    // Parse the JSON string
    JSON_Value *root_value = json_parse_string_with_comments(chtfile);
    
    if (root_value == NULL || json_value_get_type(root_value) != JSONObject) {
        jgrf_log(JG_LOG_WRN, "Failed to parse cheat file\n");
        return;
    }
    
    // Retrieve the cheats array
    JSON_Object *parent = json_value_get_object(root_value);
    JSON_Array *cheatarray = json_object_get_array(parent, "cheats");
    
    // Loop through all the cheats
    for (size_t i = 0; i < json_array_get_count(cheatarray); ++i) {
        JSON_Object *cheat = json_array_get_object(cheatarray, i);
        
        // Apply any cheats set to "enabled"
        if (json_object_get_boolean(cheat, "enabled")) {
            jgrf_log(JG_LOG_DBG,
                "Cheat: %s\n", json_object_get_string(cheat, "desc"));
            
            // Enable all codes in the array
            JSON_Array *codes = json_object_get_array(cheat, "codes");
            for (size_t j = 0; j < json_array_get_count(codes); ++j)
                jgrf_cheat_set(json_array_get_string(codes, j));
        }
    }
    
    chtactive = 1;
    jgrf_log(JG_LOG_SCR, "Cheats Activated");
    
    // Free resources associated with parsing the JSON string
    json_value_free(root_value);
}

void jgrf_cheats_deactivate(void) {
    // If cheats are not loaded, simply return
    if (!chtloaded) {
        jgrf_log(JG_LOG_WRN, "No cheats to deactivate\n");
        return;
    }
    else if (!chtactive) {
        return;
    }
    
    jgrf_cheat_clear();
    chtactive = 0;
    jgrf_log(JG_LOG_SCR, "Cheats Deactivated");
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
    chtloaded = 1;
    jgrf_cheats_activate();
}
