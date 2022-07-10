/*
 * Copyright (c) 2020-2022 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

#ifdef __APPLE__
void memset_pattern4(void *__b, const void *__pattern4, size_t __len);
#endif

#include <SDL.h>
#include <jg/jg.h>
#include <speex/speex_resampler.h>

#include "jgrf.h"
#include "audio.h"
#include "cli.h"
#include "settings.h"
#include "wave_writer.h"

#define SPFTOLERANCE 28 // Divergence tolerance between current and average SPF

static jgrf_gdata_t *gdata = NULL;

// Pointer to audio information from the emulator core
static jg_audioinfo_t *audinfo = NULL;

static SDL_AudioSpec spec, obtained;
static SDL_AudioDeviceID dev;

static SpeexResamplerState *resampler = NULL;
static int err;

static wave_writer *ww;
static wave_writer_format wwformat;
static wave_writer_error wwerror;

static void *corebuf = NULL; // Audio buffer for the core
static int16_t *rsbuf = NULL; // Buffer for audio data to be resampled
static int16_t *outbuf = NULL; // Buffer for final output samples

static mavg_t mavg_in = { 0, 0, {0} }; // Moving Average for input sizes
static ringbuf_t rbuf_in = { 0, 0, 0, 0, NULL }; // Ring buffer (input audio)
static ringbuf_t rbuf_out = { 0, 0, 0, 0, NULL }; // Ring buffer (output audio)
static struct timespec req, rem;

static int corefps = 0; // Emulator core's framerate rounded to nearest int
static int ma_offset = 0; // Offset the input moving average chunk size
static int out_offset = 0; // Offset the output rate

extern int bmark; // External benchmark mode variable
extern int fforward; // External fast-forward level
extern int waveout; // Wave File Output

// Add a new sample chunk size to the moving average and recalculate
static inline void jgrf_audio_mavg(mavg_t *mavg, size_t newval) {
    mavg->buf[mavg->pos++] = newval;

    if (mavg->pos == MAVGSIZE)
        mavg->pos = 0;

    double sum = 0;

    for (uint32_t i = 0; i < MAVGSIZE; ++i)
        sum += mavg->buf[i];

    double divisor = MAVGSIZE;
    mavg->avg = sum / divisor;
}

// Seed the moving average input sample chunk size
static inline void jgrf_audio_mavg_seed(mavg_t *mavg, size_t seed) {
    for (uint32_t i = 0; i < MAVGSIZE; ++i)
        mavg->buf[i] = seed;
    jgrf_audio_mavg(mavg, seed);
}

// Enqueue and Dequeue samples
static inline int16_t jgrf_rbuf_deq(ringbuf_t *rbuf) {
    if (rbuf->cursize == 0)
        return 0;

    int16_t sample = rbuf->buffer[rbuf->head];
    rbuf->head = (rbuf->head + 1) % rbuf->bufsize;
    --rbuf->cursize;
    return sample;
}

static inline void jgrf_rbuf_enq(ringbuf_t *rbuf, int16_t *data, size_t size) {
    for (uint32_t i = 0; i < size; ++i) {
        rbuf->buffer[rbuf->tail] = data[i];
        rbuf->tail = (rbuf->tail + 1) % rbuf->bufsize;
        ++rbuf->cursize;
        if (rbuf->cursize >= rbuf->bufsize - 1)
            break;
    }
}

static inline void jgrf_rbuf_enqf(ringbuf_t *rbuf, float *data, size_t size) {
    for (uint32_t i = 0; i < size; ++i) {
        data[i] *= 32768.0;

        if (data[i] >= 32767.0)
            rbuf->buffer[rbuf->tail] = 32767;
        else if (data[i] <= -32768.0)
            rbuf->buffer[rbuf->tail] = -32768;
        else
            rbuf->buffer[rbuf->tail] = (int16_t)(lrintf(data[i]));

        rbuf->tail = (rbuf->tail + 1) % rbuf->bufsize;
        ++rbuf->cursize;
        if (rbuf->cursize >= rbuf->bufsize - 1)
            break;
    }
}

// Set timing information to align audio processing with core framerate
void jgrf_audio_timing(double frametime) {
    int spf = (audinfo->rate / (int)(frametime + 0.5)) * audinfo->channels;
    corefps = frametime + 0.5;

    if (abs(spf - (int)(mavg_in.avg + 0.5)) >= SPFTOLERANCE) {
        int oldavg = (int)mavg_in.avg;
        jgrf_audio_mavg_seed(&mavg_in, spf);
        jgrf_log(JG_LOG_DBG,
            "Moving Averge set: %f fps, %d spf (old: %d, diff: %d)\n",
            frametime, spf, oldavg, abs(spf - oldavg));
    }
}

// Callback used by core to tell the frontend how many samples are ready
void jgrf_audio_cb_core(size_t in_size) {
    /* If Benchmark mode is set, or the  core calls this function with no
       samples ready, do nothing.
    */
    if (bmark || !in_size)
        return;

    // Adjust input moving average calculation to reflect this input size
    jgrf_audio_mavg(&mavg_in, in_size);

    // Try to keep the queue around the size of the ideal samples per frame
    size_t spf = (audinfo->rate / corefps) * audinfo->channels;

    // Input moving average samples per frame - make sure the number is even
    uint32_t ma_insamps =
        (uint32_t)(mavg_in.avg) + ((uint32_t)(mavg_in.avg) % 2);

    // Control the size of the input queue
    if (rbuf_in.cursize == 0)
        ma_offset = -(in_size >> 1); // Buffer half of the samples if empty
    else if (rbuf_in.cursize < spf)
        ma_offset = -audinfo->channels; // Eat Less
    else if (rbuf_in.cursize > (spf + (spf >> 2)))
        ma_offset = audinfo->channels; // Eat More
    else if (rbuf_in.cursize == spf)
        ma_offset = 0; // Goldilocks Zone

    ma_insamps += ma_offset;

    // Calculate rates for use in resampling
    uint32_t in_rate = (ma_insamps * corefps) / audinfo->channels;
    uint32_t out_rate = audinfo->rate / (fforward ? fforward + 1 : 1);

    // Manage the size of the output buffer to avoid underruns
    if (rbuf_out.cursize < spf) // Push More
        out_offset = 3 * corefps;
    else if (rbuf_out.cursize < (spf + (spf >> 1))) // Push More
        out_offset = 2 * corefps;
    else if (rbuf_out.cursize < spf * 2) // Push More
        out_offset = corefps;
    else if (rbuf_out.cursize > spf * 5) // Push Less
        out_offset = -corefps;
    else if (rbuf_out.cursize > spf * 3) // Goldilocks Zone
        out_offset = 0;

    out_rate += out_offset;

    // Change the resampling ratio
    err = speex_resampler_set_rate_frac(resampler,
        in_rate, out_rate, in_rate, out_rate);

    // Debug output
    //jgrf_log(JG_LOG_DBG, "i - r: %d, s: %d, c: %d, ma: %d (%f), fps: %d\n",
    //  in_rate, in_size, rbuf_in.cursize, ma_insamps, mavg_in.avg, corefps);

    uint32_t out_size = (ma_insamps * (double)out_rate) / (double)in_rate;

    // Enqueue samples from the core for resampling and output
    if (audinfo->sampfmt == JG_SAMPFMT_INT16)
        jgrf_rbuf_enq(&rbuf_in, audinfo->buf, in_size);
    else
        jgrf_rbuf_enqf(&rbuf_in, audinfo->buf, in_size);

    // Dequeue the moving average number of samples to be resampled
    for (uint32_t i = 0; i < ma_insamps; ++i)
        rsbuf[i] = jgrf_rbuf_deq(&rbuf_in);

    // Perform resampling
    if (audinfo->channels == 2) {
        ma_insamps >>= 1;
        out_size >>= 1;

        err = speex_resampler_process_interleaved_int(resampler,
            rsbuf, &ma_insamps, outbuf, &out_size);

        out_size <<= 1;
    }
    else {
        err = speex_resampler_process_int(resampler, 0,
            rsbuf, &ma_insamps, outbuf, &out_size);
    }

    // Queue the resampled audio for output
    while ((rbuf_out.cursize + out_size) >= rbuf_out.bufsize)
        nanosleep(&req, &rem);

    SDL_LockAudioDevice(dev);
    jgrf_rbuf_enq(&rbuf_out, outbuf, out_size);
    SDL_UnlockAudioDevice(dev);

    //jgrf_log(JG_LOG_DBG, "o - r: %d, s: %d, c: %d\n",
    //  out_rate, out_size, rbuf_out.cursize);
}

// Pass the core's audio information into the frontend
void jgrf_audio_set_info(jg_audioinfo_t *ptr) {
    audinfo = ptr;
}

// SDL Audio Callback for 16-bit signed integer samples
static void jgrf_audio_cb_int16(void *userdata, uint8_t *stream, int len) {
    if (userdata) {}
    int16_t *ostream = (int16_t*)stream;

    for (size_t i = 0; i < len / sizeof(int16_t); ++i)
        ostream[i] = jgrf_rbuf_deq(&rbuf_out);
}

// SDL Audio Callback for 16-bit signed integer samples plus wave output
static void jgrf_audio_cb_waveout(void *userdata, uint8_t *stream, int len) {
    if (userdata) {}
    int16_t *ostream = (int16_t*)stream;

    for (size_t i = 0; i < len / sizeof(int16_t); ++i)
        ostream[i] = jgrf_rbuf_deq(&rbuf_out);

    wave_writer_put_samples(ww, len / (sizeof(int16_t) * audinfo->channels),
        (void*)stream);
}

// Initialize the audio device and allocate buffers
int jgrf_audio_init(void) {
    settings_t *settings = jgrf_get_settings();

    // Set up Wave Writer
    if (waveout) {
        wwformat.num_channels = audinfo->channels;
        wwformat.sample_rate = audinfo->rate;
        wwformat.sample_bits = 16;
        ww = wave_writer_open(jgrf_cli_wave(), &wwformat, &wwerror);
    }

    spec.channels = audinfo->channels;
    spec.freq = audinfo->rate;
    spec.silence = 0;
    spec.samples = 512;
    spec.userdata = 0;
    #if SDL_BYTEORDER == SDL_BIG_ENDIAN
    spec.format = AUDIO_S16MSB;
    #else
    spec.format = AUDIO_S16LSB;
    #endif
    spec.callback = waveout ? jgrf_audio_cb_waveout : jgrf_audio_cb_int16;

    // Open the Audio Device
    dev = SDL_OpenAudioDevice(NULL, 0, &spec, &obtained,
        SDL_AUDIO_ALLOW_ANY_CHANGE);

    // Set up the Resampler
    resampler = speex_resampler_init(audinfo->channels,
        audinfo->rate, audinfo->rate, settings->audio_rsqual.val, &err);

    if (dev && resampler)
        jgrf_log(JG_LOG_INF, "Audio: %dHz %s, Speex %d\n", spec.freq,
            spec.channels == 1 ? "Mono" : "Stereo", settings->audio_rsqual.val);
    else
        jgrf_log(JG_LOG_WRN, "Audio: Error opening audio device.\n");

    // Seed the moving averages
    jgrf_audio_mavg_seed(&mavg_in, audinfo->spf);

    if (!corefps)
        corefps = (audinfo->rate / audinfo->spf) * audinfo->channels;

    // Set delay time in nanoseconds
    req.tv_nsec = 100000; // 1/10th of a millisecond

    // Allocate audio buffers
    gdata = jgrf_gdata_ptr();

    size_t bufsize = audinfo->spf * sizeof(int16_t) * 4;

    if (!(gdata->hints & JG_HINT_AUDIO_INTERNAL))
        corebuf = (void*)calloc(1, bufsize << audinfo->sampfmt);

    rsbuf = (void*)calloc(1, bufsize);
    outbuf = (void*)calloc(1, bufsize * 2);

    size_t rbufsize = audinfo->spf * 6;
    rbuf_in.bufsize = rbufsize;
    rbuf_in.buffer = (void*)calloc(1, rbufsize * sizeof(int16_t));
    rbuf_out.bufsize = rbufsize;
    rbuf_out.buffer = (void*)calloc(1, rbufsize * sizeof(int16_t));

    // Pass the core audio buffer into the emulator if needed
    audinfo->buf = corebuf;

    SDL_PauseAudioDevice(dev, 1); // Setting to 0 unpauses

    return 1;
}

// Unpause the audio device
void jgrf_audio_unpause(void) {
    SDL_PauseAudioDevice(dev, 0);
}

// Deinitialize the audio device and free buffers
void jgrf_audio_deinit() {
    if (waveout) wave_writer_close(ww, &wwerror);
    if (dev) SDL_CloseAudioDevice(dev);
    if (resampler) speex_resampler_destroy(resampler);

    if (!(gdata->hints & JG_HINT_AUDIO_INTERNAL))
        if (corebuf) free(corebuf);

    if (rbuf_in.buffer) free(rbuf_in.buffer);
    if (rbuf_out.buffer) free(rbuf_out.buffer);
    if (rsbuf) free(rsbuf);
    if (outbuf) free(outbuf);
}
