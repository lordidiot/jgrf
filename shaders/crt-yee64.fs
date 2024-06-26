/*
MIT License

CRT-Yee64
Copyright (c) 2017 Lucas Melo

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

precision highp float;

uniform sampler2D source;
uniform vec4 sourceSize;

in vec2 texCoord;

out vec4 fragColor;

void main() {
    //Declare parameters
    //pixelSize
    vec4 c0 = sourceSize.xyyy;
    //textureSize
    vec4 c1 = sourceSize;
    //viewSize
    vec4 c2 = sourceSize;

    const vec4 c3 = vec4(-1., 0.5, 1.25, 0.);
    const vec4 c4 = vec4(0.5, -0.5, 1.5, -3.);
    const vec4 c5 = vec4(-1., 1., -2., 2.);
    const vec4 c6 = vec4(-3., -8., 720., 0.166666672);
    const vec4 c7 = vec4(-0.333000004, -0.666000009, 0.899999976, 1.20000005);
    const vec4 c8 = vec4(1.5, 0.5, 2.5, 0.899999976);

    //Declare registers
    vec4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9;

    //Code starts here
    vec4 v0 = texCoord.xyyy;
    //dcl_2d s0
    r0.z = c3.w;
    r1.x = 1.0 / c0.x;
    r1.y = 1.0 / c0.y;
    r1.xy = (r1 * c1).xy;
    r1.xy = (r1 * v0).xy;
    r2.x = 1.0 / c1.x;
    r2.y = 1.0 / c1.y;
    r1.zw = (r2.xyxy * c0.xyxy).zw;
    r1.zw = (r1 * r1.xyxy).zw;
    r1.xy = (r1 * c2).xy;
    r2.zw = (r1 * c1.xyxy).zw;
    r2.zw = fract(r2).zw;
    r0.xy = (-r2.zwzw).xy;
    r3.xy = (r1.zwzw * c1 + r0.xzzw).xy;
    r4.yz = (r1.xzww * c1.xxyw + r0.xzyw).yz;
    r3.z = r0.y + r3.y;
    r5 = r3.xzxz + -c4.zyxy;
    r3 = r3.xzxz + c8.xyzy;
    r3 = r2.xyxy * r3;
    r5 = r2.xyxy * r5;
    r6 = texture(source, r5.zw);
    r5 = texture(source, r5.xy);
    r5.xyz = (r5 * c3.zzzz).xyz;
    r7 = r1.zwzw * c1.xyxy + r0.xyxy;
    r0.zw = (r1 * c1.xyxy + - r7).zw;
    r8.x = c3.x;
    r1.zw = (r1 * c1.xyxy + r8.xxxx).zw;
    r1.zw = (r0.xyxy + r1).zw;
    r4.x = r0.x + r4.y;
    r4 = r4.xzxz + c4.xyxz;
    r4 = r2.xyxy * r4;
    r0.xy = (r1.zwzw + c3.yyyy).xy;
    r0.xy = (r2 * r0).xy;
    r8 = texture(source, r0.xy);
    r8.xyz = (r8 * c3.zzzz).xyz;
    r0.xy = (r0.zwzw + -c3.yyyy).xy;
    r9 = -r0.xxxx + c5;
    r9 = r9 * r9;
    r9 = r9 * c4.wwww;
    r0.z = pow(2.0, r9.x);
    r6.xyz = (r6 * r0.zzzz).xyz;
    r6.xyz = (r6 * c3.zzzz).xyz;
    r0.w = pow(2.0, r9.z);
    r5.xyz = (r5 * r0.wwww + r6).xyz;
    r0.w = r0.z + r0.w;
    r6 = r7.zwzw + c4.zyxx;
    r7 = r7 + c4.yzzz;
    r7 = r2.xyxy * r7;
    r2 = r2.xyxy * r6;
    r6 = texture(source, r2.zw);
    r2 = texture(source, r2.xy);
    r2.xyz = (r2 * c3.zzzz).xyz;
    r1.zw = (r0.xyxy * r0.xyxy).zw;
    r0.xy = (-r0.yyyy + c5).xy;
    r0.xy = (r0 * r0).xy;
    r0.xy = (r0 * c6.yyyy).xy;
    r1.zw = (r1 * c6.xyxy).zw;
    r1.z = pow(2.0, r1.z);
    r1.w = pow(2.0, r1.w);
    r6.xyz = (r6 * r1.zzzz).xyz;
    r5.xyz = (r6 * c3.zzzz + r5).xyz;
    r6 = texture(source, r3.xy);
    r3 = texture(source, r3.zw);
    r3.xyz = (r3 * c3.zzzz).xyz;
    r2.w = pow(2.0, r9.y);
    r3.w = pow(2.0, r9.w);
    r6.xyz = (r6 * r2.wwww).xyz;
    r5.xyz = (r6 * c3.zzzz + r5).xyz;
    r3.xyz = (r3 * r3.wwww + r5).xyz;
    r0.w = r0.w + r1.z;
    r0.w = r2.w + r0.w;
    r0.w = r3.w + r0.w;
    r0.w = 1.0 / r0.w;
    r3.xyz = (r0.wwww * r3).xyz;
    r3.xyz = (r1.wwww * r3).xyz;
    r5 = texture(source, r4.xy);
    r4 = texture(source, r4.zw);
    r4.xyz = (r1.zzzz * r4).xyz;
    r4.xyz = (r4 * c3.zzzz).xyz;
    r5.xyz = (r5 * c3.zzzz).xyz;
    r5.xyz = (r1.zzzz * r5).xyz;
    r0.w = r0.z + r1.z;
    r0.w = r2.w + r0.w;
    r0.w = 1.0 / r0.w;
    r5.xyz = (r8 * r0.zzzz + r5).xyz;
    r2.xyz = (r2 * r2.wwww + r5).xyz;
    r2.xyz = (r0.wwww * r2).xyz;
    r0.x = pow(2.0, r0.x);
    r0.y = pow(2.0, r0.y);
    r2.xyz = (r2 * r0.xxxx + r3).xyz;
    r3 = texture(source, r7.xy);
    r5 = texture(source, r7.zw);
    r5.xyz = (r2.wwww * r5).xyz;
    r3.xyz = (r0.zzzz * r3).xyz;
    r3.xyz = (r3 * c3.zzzz + r4).xyz;
    r3.xyz = (r5 * c3.zzzz + r3).xyz;
    r0.xzw = (r0.wwww * r3.xyyz).xzw;
    r0.xyz = (r0.xzww * r0.yyyy + r2).xyz;
    r1.zw = fract(r1.xyxy).zw;
    r1.xy = (-r1.zwzw + r1).xy;
    r1.xy = (r1 + c3.yyyy).xy;
    r0.w = (r1.y * -c4.w + r1.x);
    r0.w = r0.w * c6.w;
    r0.w = fract(r0.w);
    r1.xy = (r0.wwww + c7).xy;
    r2.yz = (r1.y >= 0.0 ? c7.xzww : c7.xwzw).yz;
    r2.x = c8.w;
    r1.xyz = (r1.x >= 0.0 ? r2 : c7.wzzw).xyz;
    r1.xyz = (r0 * r1).xyz;
    r2.z = c6.z;
    r0.w = r2.z + -c2.y;
    fragColor.xyz = (r0.w >= 0.0 ? r0 : r1).xyz;
    fragColor.w = -c3.x;
}
