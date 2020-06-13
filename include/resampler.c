/*
MIT License

Copyright (c) 2019 Zhihan Gao
Copyright (c) 2020 Rupert Carmichael

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stddef.h>
#include <stdint.h>

#include "resampler.h"

size_t resamp_flt32(const float *input, float *output,
    int in_rate, int out_rate, size_t in_size, int channels) {
    
    if (input == NULL)
        return 0;
    
    size_t out_size = (size_t)(in_size * (double)out_rate / (double)in_rate);
    out_size -= out_size % channels;
    
    if (output == NULL)
        return out_size;
    
    double stepDist = ((double)in_rate / (double)out_rate);
    const uint64_t fixedFraction = (1LL << 32);
    const double normFixed = (1.0 / (1LL << 32));
    uint64_t step = ((uint64_t) (stepDist * fixedFraction + 0.5));
    uint64_t curOffset = 0;
    
    for (uint32_t i = 0; i < out_size; i++) {
        for (int c = 0; c < channels; c++) {
            *output++ = (float)(input[c] + (input[c + channels] - input[c]) *
                ((double)(curOffset >> 32) +
                ((curOffset & (fixedFraction - 1)) * normFixed)));
        }
        curOffset += step;
        input += (curOffset >> 32) * channels;
        curOffset &= (fixedFraction - 1);
    }
    
    return out_size;
}

size_t resamp_int16(const int16_t *input, int16_t *output,
    int in_rate, int out_rate, size_t in_size, int channels) {
    
    if (input == NULL)
        return 0;
    
    size_t out_size = (size_t)(in_size * (double)out_rate / (double)in_rate);
    out_size -= out_size % channels;
    
    if (output == NULL)
        return out_size;
    
    double stepDist = ((double)in_rate / (double)out_rate);
    const uint64_t fixedFraction = (1LL << 32);
    const double normFixed = (1.0 / (1LL << 32));
    uint64_t step = ((uint64_t)(stepDist * fixedFraction + 0.5));
    uint64_t curOffset = 0;
    
    for (uint32_t i = 0; i < out_size; i++) {
        for (int c = 0; c < channels; c++) {
            *output++ = (int16_t)(input[c] + (input[c + channels] - input[c]) *
                ((double)(curOffset >> 32) +
                ((curOffset & (fixedFraction - 1)) * normFixed)));
        }
        curOffset += step;
        input += (curOffset >> 32) * channels;
        curOffset &= (fixedFraction - 1);
    }
    
    return out_size;
}
