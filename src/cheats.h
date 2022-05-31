/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef CHEATS_H
#define CHEATS_H

void jgrf_cheats_activate(void);
void jgrf_cheats_deactivate(void);
void jgrf_cheats_deinit(void);
void jgrf_cheats_init(void (*)(void), void (*)(const char *));

#endif
