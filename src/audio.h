/*
 * Copyright (c) 2020 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef AUDIO_H
#define AUDIO_H

#define MAVGSIZE 60 * 2 // 60 * N second moving average

// Moving Average
typedef struct mavg_t {
    double avg; // The moving average
    uint32_t pos; // Position in the buffer
    size_t buf[MAVGSIZE]; // The buffer
} mavg_t;

// Ring Buffer (Audio Queue)
typedef struct ringbuf_t {
    uint32_t head; // Head position
    uint32_t tail; // Tail position
    uint32_t cursize; // Current queue size
    uint32_t bufsize; // Buffer size
    void *buffer; // Pointer to the buffer
} ringbuf_t;

void jgrf_audio_set_info(jg_audioinfo_t*);
int jgrf_audio_underflow();
void jgrf_audio_timing(double);
void jgrf_audio_cb_core(size_t);
void jgrf_audio_unpause();
void jgrf_audio_init();
void jgrf_audio_deinit();

#endif
