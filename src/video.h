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

#ifndef VIDEO_H
#define VIDEO_H

extern void (*jgrf_video_create)(void);
extern int (*jgrf_video_init)(void);
extern void (*jgrf_video_deinit)(void);
extern void (*jgrf_video_fullscreen)(void);
extern void (*jgrf_video_render)(int);
extern void (*jgrf_video_resize)(void);
extern void (*jgrf_video_get_scale_params)(float*, float*, float*, float*);
extern void (*jgrf_video_set_cursor)(int);
extern jg_videoinfo_t* (*jgrf_video_get_info)(void);
extern void (*jgrf_video_set_info)(jg_videoinfo_t*);
extern void (*jgrf_video_swapbuffers)(void);
extern void (*jgrf_video_text)(int, int, const char*);
extern void (*jgrf_video_rehash)(void);

void jgrf_video_setfuncs(void);
void jgrf_video_icon_load(SDL_Window*);
void jgrf_video_screenshot(void);

#endif
