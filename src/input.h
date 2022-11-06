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

#ifndef INPUT_H
#define INPUT_H

#define DEADZONE 5120 // Deadzone for axis input
#define BDEADZONE 16384 // Deadzone for axes acting as buttons

int jgrf_input_init(void);
void jgrf_input_deinit(void);
void jgrf_input_query(jg_inputinfo_t *(*)(int));
void jgrf_input_set_audio(void (*)(int, const int16_t*, size_t));
void jgrf_input_set_states(void (*)(jg_inputstate_t*, int));
void jgrf_input_handler(SDL_Event*);
void jgrf_input_rumble(int, float, size_t);

void jgrf_input_map_axis(int, uint32_t, const char*);
void jgrf_input_map_button(int, uint32_t, const char*);

void jgrf_input_config_enable(int);
void jgrf_input_menu_enable(int);
void jgrf_input_config(int);
jg_inputinfo_t **jgrf_input_info_ptr(void);

#endif
