#ifndef RESAMPLER_H
#define RESAMPLER_H

size_t resamp_flt32(const float*, float*, int, int, size_t, int);
size_t resamp_int16(const int16_t*, int16_t*, int, int, size_t, int);

#endif
