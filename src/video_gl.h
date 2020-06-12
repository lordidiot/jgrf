/*
 * Copyright (c) 2020 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef VIDEO_GL_H
#define VIDEO_GL_H

void jgrf_video_gl_create();
int jgrf_video_gl_init();
void jgrf_video_gl_deinit();
void jgrf_video_gl_fullscreen();
void jgrf_video_gl_render();
void jgrf_video_gl_resize();
void jgrf_video_gl_get_scale_params(float*, float*, float*, float*);
void jgrf_video_gl_set_cursor(int);
jg_videoinfo_t* jgrf_video_gl_get_info();
void jgrf_video_gl_set_info(jg_videoinfo_t*);
void jgrf_video_gl_setup();
void jgrf_video_gl_swapbuffers();
void jgrf_video_gl_text(int, int, const char*);
void *jgrf_video_gl_get_pixels(int*, int*);

#endif
