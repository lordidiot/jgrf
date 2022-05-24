/*
 * Copyright (c) 2020 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef CLI_H
#define CLI_H

const char *jgrf_cli_core(void);
const char *jgrf_cli_wave(void);
void jgrf_cli_override(void);
void jgrf_cli_parse(int, char**);
void jgrf_cli_usage(void);

#endif
