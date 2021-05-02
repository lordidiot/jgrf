/*
 * Copyright (c) 2020 Rupert Carmichael
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#ifdef __APPLE__
void memset_pattern4(void *__b, const void *__pattern4, size_t __len);
#endif

#include <SDL.h>
#include <jg/jg.h>
#include <speex/speex_resampler.h>

#include "jgrf.h"
#include "audio.h"
#include "settings.h"

#define SPFTOLERANCE 32 // Divergence tolerance between current and average SPF

static jgrf_gdata_t *gdata = NULL;

// Pointer to audio information from the emulator core
static jg_audioinfo_t *audinfo = NULL;

static SDL_AudioSpec spec, obtained;
static SDL_AudioDeviceID dev;

static SpeexResamplerState *resampler = NULL;
static int err;

static void *corebuf = NULL; // Audio buffer for the core
static void *rsbuf = NULL; // Buffer for audio data to be resampled
static void *outbuf = NULL; // Buffer for final output samples

static size_t sampsize = sizeof(int16_t); // Size of samples - int16 or float32

static mavg_t mavg_in = { 0, 0, {0} }; // Moving Average for input sizes
static ringbuf_t rbuf_in = { 0, 0, 0, 0, NULL }; // Ring buffer (input audio)
static ringbuf_t rbuf_out = { 0, 0, 0, 0, NULL }; // Ring buffer (output audio)
static struct timespec req, rem;

static int corefps = 0; // Emulator core's framerate rounded to nearest int
static int ma_offset = 0; // Offset the moving average to control buffer sizes

extern int fforward; // External fast-forward level

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
static inline int16_t jgrf_rbuf_deq_int16(ringbuf_t *rbuf) {
    if (rbuf->cursize == 0)
        return 0;
    
    int16_t *buf = (int16_t*)rbuf->buffer;
    int16_t sample = buf[rbuf->head];
    rbuf->head = (rbuf->head + 1) % rbuf->bufsize;
    --rbuf->cursize;
    return sample;
}

static inline float jgrf_rbuf_deq_flt32(ringbuf_t *rbuf) {
    if (rbuf->cursize == 0)
        return 0;
    
    float *buf = (float*)rbuf->buffer;
    float sample = buf[rbuf->head];
    rbuf->head = (rbuf->head + 1) % rbuf->bufsize;
    --rbuf->cursize;
    return sample;
}

static void jgrf_rbuf_enq_int16(ringbuf_t *rbuf, int16_t *data, size_t size) {
    int16_t *buf = (int16_t*)rbuf->buffer;
    for (uint32_t i = 0; i < size; ++i) {
        buf[rbuf->tail] = data[i];
        rbuf->tail = (rbuf->tail + 1) % rbuf->bufsize;
        ++rbuf->cursize;
        if (rbuf->cursize >= rbuf->bufsize - 1)
            break;
    }
}

static void jgrf_rbuf_enq_flt32(ringbuf_t *rbuf, float *data, size_t size) {
    float *buf = (float*)rbuf->buffer;
    for (uint32_t i = 0; i < size; ++i) {
        buf[rbuf->tail] = data[i];
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
    
    if (abs(spf - (int)(mavg_in.avg + 0.5)) > SPFTOLERANCE) {
        int oldavg = (int)mavg_in.avg;
        jgrf_audio_mavg_seed(&mavg_in, spf);
        jgrf_log(JG_LOG_DBG,
            "Moving Averge set: %f fps, %d spf (old: %d, diff: %d)\n",
            frametime, spf, oldavg, abs(spf - oldavg));
    }
}

// Callback used by core to tell the frontend how many samples are ready
void jgrf_audio_cb_core(size_t in_size) {
    // If the core calls this function with no samples ready, do nothing
    if (!in_size)
        return;
    
    // Adjust input moving average calculation to reflect this input size
    jgrf_audio_mavg(&mavg_in, in_size);
    
    // Try to keep the queue around the size of the ideal samples per frame
    size_t spf = (audinfo->rate / corefps) * audinfo->channels;
    
    // Input moving average samples per frame - make sure the number is even
    uint32_t ma_insamps =
        (uint32_t)(mavg_in.avg) + ((uint32_t)(mavg_in.avg) % 2);
    
    // Baby steps if the queue size is diverging from the desired size
    if (rbuf_in.cursize == spf)
        ma_offset = 0;
    else if (rbuf_in.cursize < spf)
        ma_offset = -audinfo->channels; // Eat Less
    else if (rbuf_in.cursize > (spf + 200))
        ma_offset = audinfo->channels; // Eat More
    
    ma_insamps += ma_offset;
    
    // Calculate rates for use in resampling
    uint32_t in_rate = (ma_insamps * corefps) / audinfo->channels;
    uint32_t out_rate = audinfo->rate / (fforward ? fforward + 1 : 1);
    
    // Manage the size of the output buffer to avoid underruns
    if (rbuf_out.cursize < spf * 2)
        out_rate += 30 << audinfo->channels; // Push More
    else if (rbuf_out.cursize > spf * 4)
        out_rate -= 45 << audinfo->channels; // Push Less
    else if (rbuf_out.cursize > spf * 3)
        out_rate -= 30 << audinfo->channels; // Push Less
    
    // Change the resampling ratio
    err = speex_resampler_set_rate_frac(resampler,
        in_rate, out_rate, in_rate, out_rate);
    
    // Debug output
    //jgrf_log(JG_LOG_DBG, "i - r: %d, s: %d, c: %d, ma: %d (%f), fps: %d\n",
    //  in_rate, in_size, rbuf_in.cursize, ma_insamps, mavg_in.avg, corefps);
    
    uint32_t out_size = (ma_insamps * (double)out_rate) / (double)in_rate;
    
    // Resample the input to the proper output rate
    switch (audinfo->sampfmt) {
        default: case JG_SAMPFMT_INT16: {
            jgrf_rbuf_enq_int16(&rbuf_in, audinfo->buf, in_size);
            
            // Dequeue the moving average number of samples to be resampled
            int16_t *rsbuf_p = (int16_t*)rsbuf;
            for (uint32_t i = 0; i < ma_insamps; ++i)
                rsbuf_p[i] = jgrf_rbuf_deq_int16(&rbuf_in);
            
            // Perform resampling
            if (audinfo->channels == 2) {
                ma_insamps >>= 1;
                out_size >>= 1;
                
                err = speex_resampler_process_interleaved_int(resampler,
                    (spx_int16_t*)rsbuf, &ma_insamps,
                    (spx_int16_t*)outbuf, &out_size);
                
                out_size <<= 1;
            }
            else {
                err = speex_resampler_process_int(resampler, 0,
                    (spx_int16_t*)rsbuf, &ma_insamps,
                    (spx_int16_t*)outbuf, &out_size);
            }
            
            // Queue the resampled audio for output
            while ((rbuf_out.cursize + out_size) >= rbuf_out.bufsize)
                nanosleep(&req, &rem);
            
            SDL_LockAudioDevice(dev);
            jgrf_rbuf_enq_int16(&rbuf_out, (int16_t*)outbuf, out_size);
            SDL_UnlockAudioDevice(dev);
            break;
        }
        case JG_SAMPFMT_FLT32: {
            jgrf_rbuf_enq_flt32(&rbuf_in, audinfo->buf, in_size);
            
            // Dequeue the moving average number of samples to be resampled
            float *rsbuf_p = (float*)rsbuf;
            for (uint32_t i = 0; i < ma_insamps; ++i)
                rsbuf_p[i] = jgrf_rbuf_deq_flt32(&rbuf_in);
            
            // Perform resampling
            if (audinfo->channels == 2) {
                ma_insamps >>= 1;
                out_size >>= 1;
                
                err = speex_resampler_process_interleaved_float(resampler,
                    (float*)rsbuf, &ma_insamps, (float*)outbuf, &out_size);
                
                out_size <<= 1;
            }
            else {
                err = speex_resampler_process_float(resampler, 0,
                    (float*)rsbuf, &ma_insamps, (float*)outbuf, &out_size);
            }
            
            // Queue the resampled audio for output
            while ((rbuf_out.cursize + out_size) >= rbuf_out.bufsize)
                nanosleep(&req, &rem);
            
            SDL_LockAudioDevice(dev);
            jgrf_rbuf_enq_flt32(&rbuf_out, (float*)outbuf, out_size);
            SDL_UnlockAudioDevice(dev);
            break;
        }
    }
    
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
    
    for (size_t i = 0; i < len / sampsize; ++i)
        ostream[i] = jgrf_rbuf_deq_int16(&rbuf_out);
}

// SDL Audio Callback for 32-bit floating point samples
static void jgrf_audio_cb_flt32(void *userdata, uint8_t *stream, int len) {
    if (userdata) {}
    float *ostream = (float*)stream;
    
    for (size_t i = 0; i < len / sampsize; ++i)
        ostream[i] = jgrf_rbuf_deq_flt32(&rbuf_out);
}

// Initialize the audio device and allocate buffers
int jgrf_audio_init(void) {
    settings_t *settings = jgrf_get_settings();
    
    spec.channels = audinfo->channels;
    spec.freq = audinfo->rate;
    spec.silence = 0;
    spec.samples = 512;
    spec.userdata = 0;
    spec.format = audinfo->sampfmt == JG_SAMPFMT_INT16 ?
        AUDIO_S16SYS : AUDIO_F32SYS;
    spec.callback = audinfo->sampfmt == JG_SAMPFMT_INT16 ?
        jgrf_audio_cb_int16 : jgrf_audio_cb_flt32;
    
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
    
    // Set the sample size
    sampsize = audinfo->sampfmt == JG_SAMPFMT_INT16 ?
        sizeof(int16_t) : sizeof(float);
    
    // Allocate audio buffers
    gdata = jgrf_gdata_ptr();
    
    size_t bufsize = audinfo->spf * sampsize * 4;
    
    if (!(gdata->hints & JG_HINT_AUDIO_INTERNAL))
        corebuf = (void*)calloc(1, bufsize);
    
    rsbuf = (void*)calloc(1, bufsize);
    outbuf = (void*)calloc(1, bufsize * 2);
    
    size_t rbufsize = audinfo->spf * 6;
    rbuf_in.bufsize = rbufsize;
    rbuf_in.buffer = (void*)calloc(1, rbufsize * sampsize);
    rbuf_out.bufsize = rbufsize;
    rbuf_out.buffer = (void*)calloc(1, rbufsize * sampsize);
    
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
    if (dev) SDL_CloseAudioDevice(dev);
    if (resampler) speex_resampler_destroy(resampler);
    
    if (!(gdata->hints & JG_HINT_AUDIO_INTERNAL))
        if (corebuf) free(corebuf);
    
    if (rbuf_in.buffer) free(rbuf_in.buffer);
    if (rbuf_out.buffer) free(rbuf_out.buffer);
    if (rsbuf) free(rsbuf);
    if (outbuf) free(outbuf);
}
