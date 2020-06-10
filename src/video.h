/*
 * Copyright (c) 2020 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef VIDEO_H
#define VIDEO_H

extern void (*jgrf_video_create)();
extern void (*jgrf_video_init)();
extern void (*jgrf_video_deinit)();
extern void (*jgrf_video_fullscreen)();
extern void (*jgrf_video_render)();
extern void (*jgrf_video_resize)();
extern void (*jgrf_video_get_scale_params)(float*, float*, float*, float*);
extern void (*jgrf_video_set_cursor)(int);
extern jg_videoinfo_t* (*jgrf_video_get_info)();
extern void (*jgrf_video_set_info)(jg_videoinfo_t*);
extern void (*jgrf_video_swapbuffers)();
extern void (*jgrf_video_text)(int, int, const char*);

void jgrf_video_setfuncs();
void jgrf_video_icon_load(SDL_Window*);
void jgrf_video_screenshot();

#endif
