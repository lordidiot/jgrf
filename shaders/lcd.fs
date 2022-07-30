/*
MIT License

Copyright (c) 2020-2022 Rupert Carmichael

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
// Based on Public Domain work by hunterk

precision highp float;

uniform sampler2D source;
uniform vec4 sourceSize;

in vec2 texCoord;
out vec4 fragColor;

vec3 mask_weights(vec2 coord, float mask_intensity) {
    vec3 weights = vec3(1.,1.,1.);
    float on = 1.;
    float off = 1.-mask_intensity;
    vec3 red     = vec3(on,  off, off);
    vec3 green   = vec3(off, on,  off);
    vec3 blue    = vec3(off, off, on );
    vec3 black   = vec3(off, off, off);
    int w, z = 0;

    vec3 lcdmask_x1[4] = vec3[](red, green, blue, black);
    vec3 lcdmask_x2[4] = vec3[](red, green, blue, black);
    vec3 lcdmask_x3[4] = vec3[](red, green, blue, black);
    vec3 lcdmask_x4[4] = vec3[](black, black, black, black);

    // find the vertical index
    w = int(floor(mod(coord.y, 4.0)));

    // find the horizontal index
    z = int(floor(mod(coord.x, 4.0)));

    // do a comparison
    weights =
        (w == 1) ? lcdmask_x1[z] :
        (w == 2) ? lcdmask_x2[z] :
        (w == 3) ? lcdmask_x3[z] : lcdmask_x4[z];
    return weights;
}

#define mask_str 0.4

void main() {
    vec2 tcoord = texCoord * 1.00001;
    fragColor = vec4(pow(texture(source, tcoord).rgb, vec3(1.+mask_str)) *
        mask_weights(tcoord.xy * sourceSize.xy * 4., mask_str), 1.0);
}
