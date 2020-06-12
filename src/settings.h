/*
 * Copyright (c) 2020 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct setting_t {
    int val;
    int min;
    int max;
} setting_t;

typedef struct settings_t {
    setting_t video_api;
    setting_t video_fullscreen;
    setting_t video_scale;
    setting_t video_shader;
    setting_t video_crtea_mode;
    setting_t video_crtea_masktype;
    setting_t video_crtea_maskstr;
    setting_t video_crtea_scanstr;
    setting_t video_crtea_sharpness;
    setting_t video_crtea_curve;
    setting_t video_crtea_corner;
    setting_t video_crtea_tcurve;
    setting_t misc_corelog;
    setting_t misc_frontendlog;
} settings_t;

void jgrf_settings_emu(jg_setting_t*, int);

settings_t *jgrf_get_settings();

int jgrf_settings_init();
void jgrf_settings_override(const char *);
void jgrf_settings_deinit();

#endif
