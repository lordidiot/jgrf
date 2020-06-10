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
#include <string.h>

#include <jg/jg.h>

#include "tconfig.h"

#include "jgrf.h"
#include "settings.h"

static ini_table_s *conf;
static int confchanged = 0;

static settings_t settings;

static jgrf_gdata_t *gdata;

settings_t *jgrf_get_settings() {
    return &settings;
}

// Initialize the settings to defaults and grab global data pointer
void jgrf_settings_init() {
    gdata = jgrf_gdata_ptr();
    
    // Set defaults
    settings.video_api = (setting_t){ 0, 0, 0 };
    settings.video_fullscreen = (setting_t){ 0, 0, 1 };
    settings.video_scale = (setting_t){ 3, 1, 8 };
    settings.video_shader = (setting_t){ 3, 0, 6 };
    settings.video_crtea_mode = (setting_t){ 3, 0, 4 };
    settings.video_crtea_masktype = (setting_t){ 1, 0, 3 };
    settings.video_crtea_maskstr = (setting_t){ 5, 0, 10 };
    settings.video_crtea_scanstr = (setting_t){ 6, 0, 10 };
    settings.video_crtea_sharpness = (setting_t){ 4, 0, 10 };
    settings.video_crtea_curve = (setting_t){ 0, 0, 10 };
    settings.video_crtea_corner = (setting_t){ 3, 0, 10 };
    settings.video_crtea_tcurve = (setting_t){ 10, 0, 10 };
    
    settings.misc_corelog = (setting_t){ 1, 0, 3 };
    settings.misc_frontendlog = (setting_t){ 1, 0, 3 };
}

// Read a setting with boundary check
static inline void jgrf_setting_rd(const char *s, const char *n, setting_t *t) {
    // section, name, setting
    if (ini_table_check_entry(conf, s, n)) {
        int val_orig = t->val; // Save the default value
        ini_table_get_entry_as_int(conf, s, n, &(t->val)); // Read new the value
        
        // Reset to default if out of bounds
        if ((t->val > t->max) || (t->val < t->min))
            t->val = val_orig;
    }
}

// Handle reading of settings
static void jgrf_settings_handler() {
    // Video
    //jgrf_setting_rd("video", "api", &settings.video_api);
    jgrf_setting_rd("video", "fullscreen", &settings.video_fullscreen);
    jgrf_setting_rd("video", "scale", &settings.video_scale);
    jgrf_setting_rd("video", "shader", &settings.video_shader);
    jgrf_setting_rd("video", "crtea_mode", &settings.video_crtea_mode);
    jgrf_setting_rd("video", "crtea_masktype", &settings.video_crtea_masktype);
    jgrf_setting_rd("video", "crtea_maskstr", &settings.video_crtea_maskstr);
    jgrf_setting_rd("video", "crtea_scanstr", &settings.video_crtea_scanstr);
    jgrf_setting_rd("video", "crtea_sharpness",&settings.video_crtea_sharpness);
    jgrf_setting_rd("video", "crtea_curve", &settings.video_crtea_curve);
    jgrf_setting_rd("video", "crtea_corner", &settings.video_crtea_corner);
    jgrf_setting_rd("video", "crtea_tcurve", &settings.video_crtea_tcurve);
    
    // Misc
    jgrf_setting_rd("misc", "corelog", &settings.misc_corelog);
    jgrf_setting_rd("misc", "frontendlog", &settings.misc_frontendlog);
}

// Read the general settings ini to override defaults
void jgrf_settings_read() {
    char path[192];
    snprintf(path, sizeof(path), "%ssettings.ini", gdata->configpath);
    
    conf = ini_table_create();
    
    if (!ini_table_read_from_file(conf, path)) {
        jgrf_log(JG_LOG_DBG, "Main configuration file not found: %s\n", path);
        confchanged = 1;
    }
    else
        jgrf_settings_handler();
    
    // Clean up the config data
    ini_table_destroy(conf);
}

// Read core-specific overrides for frontend settings
void jgrf_settings_override(const char *name) {
    char overridepath[256];
    snprintf(overridepath, sizeof(overridepath), "%s%s/settings.ini",
        gdata->configpath, name);
    
    conf = ini_table_create();
    
    if (!ini_table_read_from_file(conf, overridepath)) {
        jgrf_log(JG_LOG_DBG, "Override configuration file not found: %s\n",
            overridepath);
    }
    else
        jgrf_settings_handler();
    
    // Clean up the config data
    ini_table_destroy(conf);
}

// Read core-specific settings - "Emulator Settings"
void jgrf_settings_emu(jg_setting_t *emusettings, int numsettings) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s/settings.ini",
        gdata->configpath, gdata->corename);
    
    // Create config structure
    conf = ini_table_create();
    if (!ini_table_read_from_file(conf, path))
        jgrf_log(JG_LOG_DBG, "Core configuration file not found: %s\n", path);
    
    for (int i = 0; i < numsettings; i++) {
        if (ini_table_check_entry(conf, gdata->corename,
            emusettings[i].name)) {
            
            int value;
            ini_table_get_entry_as_int(conf, gdata->corename,
                emusettings[i].name, &value);
            
            if (value <= emusettings[i].max && value >= emusettings[i].min)
                emusettings[i].value = value;
            else
                jgrf_log(JG_LOG_WRN, "Setting out of range: %s = %d\n",
                    emusettings[i].name, value);
        }
    }
    
    // Clean up the config data
    ini_table_destroy(conf);
}

void jgrf_settings_deinit() {
    char path[192];
    snprintf(path, sizeof(path), "%ssettings.ini", gdata->configpath);
    
    if (confchanged) {
        // Create data structure
        conf = ini_table_create();
        
        char ibuf[4]; // Buffer to hold integers converted to strings
        
        // Video
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_fullscreen.val);
        ini_table_create_entry(conf, "video", "fullscreen", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_scale.val);
        ini_table_create_entry(conf, "video", "scale", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_shader.val);
        ini_table_create_entry(conf, "video", "shader", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_crtea_mode.val);
        ini_table_create_entry(conf, "video", "crtea_mode", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_crtea_masktype.val);
        ini_table_create_entry(conf, "video", "crtea_masktype", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_crtea_maskstr.val);
        ini_table_create_entry(conf, "video", "crtea_maskstr", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_crtea_scanstr.val);
        ini_table_create_entry(conf, "video", "crtea_scanstr", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_crtea_sharpness.val);
        ini_table_create_entry(conf, "video", "crtea_sharpness", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_crtea_curve.val);
        ini_table_create_entry(conf, "video", "crtea_curve", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_crtea_corner.val);
        ini_table_create_entry(conf, "video", "crtea_corner", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.video_crtea_tcurve.val);
        ini_table_create_entry(conf, "video", "crtea_tcurve", ibuf);
        
        // Misc
        snprintf(ibuf, sizeof(ibuf), "%d", settings.misc_corelog.val);
        ini_table_create_entry(conf, "misc", "corelog", ibuf);
        
        snprintf(ibuf, sizeof(ibuf), "%d", settings.misc_frontendlog.val);
        ini_table_create_entry(conf, "misc", "frontendlog", ibuf);
        
        ini_table_write_to_file(conf, path);
        
        // Clean up the config data
        ini_table_destroy(conf);
    }
}
