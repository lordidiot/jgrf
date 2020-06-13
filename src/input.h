/*
 * Copyright (c) 2020 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INPUT_H
#define INPUT_H

#define DEADZONE 5120 // Deadzone for axis input
#define BDEADZONE 16384 // Deadzone for axes acting as buttons

int jgrf_input_init(void);
void jgrf_input_deinit(void);
void jgrf_input_query(jg_inputinfo_t* (*get_inputinfo)(int));
void jgrf_input_set_states(void (*set_inputstate)(jg_inputstate_t*, int));
void jgrf_input_handler(SDL_Event*);
void jgrf_input_rumble(int, float, size_t);

void jgrf_input_map_axis(int, uint32_t, const char*);
void jgrf_input_map_button(int, uint32_t, const char*);

#endif
