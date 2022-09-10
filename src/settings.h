/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#define SETTINGS_FRONTEND 0x01
#define SETTINGS_EMULATOR 0x02

enum _jgrf_settings {
    AUDIO_RSQUAL,
    VIDEO_API,
    VIDEO_FULLSCREEN,
    VIDEO_SCALE,
    VIDEO_SHADER,
    VIDEO_CRTEA_MODE,
    VIDEO_CRTEA_MASKTYPE,
    VIDEO_CRTEA_MASKSTR,
    VIDEO_CRTEA_SCANSTR,
    VIDEO_CRTEA_SHARPNESS,
    VIDEO_CRTEA_CURVE,
    VIDEO_CRTEA_CORNER,
    VIDEO_CRTEA_TCURVE,
    MISC_CORELOG,
    MISC_FRONTENDLOG,
    JGRF_SETTINGS_MAX
};

void jgrf_settings_init(void);
jg_setting_t* jgrf_settings_ptr(void);
jg_setting_t* jgrf_settings_emu_ptr(size_t*);
void jgrf_settings_emu(jg_setting_t* (*)(size_t*));
void jgrf_settings_override(const char *);
void jgrf_settings_write(int);

#endif
