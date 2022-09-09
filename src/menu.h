/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef MENU_H
#define MENU_H

void jgrf_menu_display(void);
void jgrf_menu_input_handler(SDL_Event*);
void jgrf_menu_set_vinfo(jg_videoinfo_t*);

#endif
