/*
MIT License

CRT-Bespoke - Custom-tailored CRT Shader
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
// Based on Public Domain work by Timothy Lottes

#version 330 core

precision highp float;

uniform sampler2D source;
uniform vec4 sourceSize;

in vec2 texCoord;

out vec4 fragColor;

// Hardness of scanline
// -4.0 = ultrasoft, -8.0 = soft, -16.0 = medium, -24.0 = hard
float hardScan = -7.0;

// Hardness of pixels in scanline
// -2.0 = soft, -4.0 = medium, -6.0 = hard
float hardPix = -4.0;

// Shadow mask
float maskDark = 0.5;
float maskLight = 1.0;

// sRGB to Linear
// Assuing using sRGB typed textures this should not be needed
float ToLinear1(float c) {
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

vec3 ToLinear(vec3 c) { 
    return vec3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b));
}

// Linear to sRGB
// Assuing using sRGB typed textures this should not be needed
float ToSrgb1(float c) {
    return(c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055);
}

vec3 ToSrgb(vec3 c) {
    return vec3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
}

// Nearest emulated sample given floating point position and texel offset
// Also zeros off screen.
vec3 Fetch(vec2 pos,vec2 off){
    pos = (floor(pos * sourceSize.xy + off) + vec2(0.5, 0.5)) / sourceSize.xy;
    return ToLinear(1.2 * texture(source, pos.xy, -16.0).rgb);
}

// Distance in emulated pixels to nearest texel
vec2 Dist(vec2 pos) {
    pos=pos*sourceSize.xy;
    return -((pos - floor(pos)) - vec2(0.5));
}

// 1D Gaussian.
float Gaus(float pos, float scale) {
    return exp2(scale * pos * pos);
}

// 3-tap Gaussian filter along horizontal line
vec3 Horz3(vec2 pos,float off) {
    vec3 b = Fetch(pos, vec2(-1.0, off));
    vec3 c = Fetch(pos, vec2(0.0, off));
    vec3 d = Fetch(pos, vec2(1.0, off));
    float dst = Dist(pos).x;
    
    // Convert distance to weight.
    float scale = hardPix;
    float wb = Gaus(dst - 1.0, scale);
    float wc = Gaus(dst + 0.0, scale);
    float wd = Gaus(dst + 1.0, scale);
    
    // Return filtered sample.
    return ((b * wb) + (c * wc) + (d * wd)) / (wb + wc + wd);
}

// 5-tap Gaussian filter along horizontal line
vec3 Horz5(vec2 pos,float off) {
    vec3 a = Fetch(pos, vec2(-2.0, off));
    vec3 b = Fetch(pos, vec2(-1.0, off));
    vec3 c = Fetch(pos, vec2(0.0, off));
    vec3 d = Fetch(pos, vec2(1.0, off));
    vec3 e = Fetch(pos, vec2(2.0, off));
    float dst = Dist(pos).x;
    
    // Convert distance to weight
    float scale = hardPix;
    float wa = Gaus(dst - 2.0, scale);
    float wb = Gaus(dst - 1.0, scale);
    float wc = Gaus(dst + 0.0, scale);
    float wd = Gaus(dst + 1.0, scale);
    float we = Gaus(dst + 2.0, scale);
    
    // Return filtered sample
    return ((a * wa) + (b * wb) + (c * wc) + (d * wd) + (e * we)) /
        (wa + wb + wc + wd + we);
}

// Return scanline weight.
float Scan(vec2 pos,float off) {
    float dst = Dist(pos).y;
    return Gaus(dst + off, hardScan);
}

// Allow nearest three lines to affect pixel
vec3 Tri(vec2 pos) {
    vec3 a = Horz3(pos, -1.0);
    vec3 b = Horz5(pos, 0.0);
    vec3 c = Horz3(pos, 1.0);
    float wa = Scan(pos, -1.0);
    float wb = Scan(pos, 0.0);
    float wc = Scan(pos, 1.0);
    return (a * wa) + (b * wb) + (c * wc);
}

// Shadow mask
vec3 Mask(vec2 pos) {
    pos.x += pos.y * 3.0;
    vec3 mask = vec3(maskDark, maskDark, maskDark);
    pos.x = fract(pos.x / 6.0);
    if (pos.x < 0.333) mask.r = maskLight;
    else if (pos.x < 0.666) mask.g = maskLight;
    else mask.b = maskLight;
    return mask;
}

void main() {
    vec2 pos = texCoord.xy * 1.0001;
    fragColor.rgb = Tri(pos) * Mask(gl_FragCoord.xy);
    fragColor = vec4(ToSrgb(fragColor.rgb), 1.0);
}
