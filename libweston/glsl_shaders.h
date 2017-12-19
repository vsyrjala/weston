/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef GLSL_SHADERS_H
#define GLSL_SHADERS_H

#define POW3 \
	"vec3 pow3(vec3 v, float p) {\n" \
	"    return pow(v, vec3(p, p, p));\n" \
	"}\n"

#define DEGAMMA \
	"vec3 degamma(vec3 v, float p, float a, float knee, float linear) {\n" \
	"    vec3 ls = v / linear;\n" \
	"    vec3 ps = pow3((v + a) / (1.0 + a), p);\n" \
	"    return mix(ls, ps, step(knee, v));\n" \
	"}\n"

#define GAMMA \
	"vec3 gamma(vec3 l, float p, float a, float knee, float linear) {\n" \
	"    vec3 ls = l * linear;\n" \
	"    vec3 ps = (1.0 + a) * pow3(l, p) - a;\n" \
	"    return mix(ls, ps, step(knee, l));\n" \
	"}\n"

#define ST2084_EOTF \
	"vec3 st2084_eotf(vec3 v) {\n" \
	"    float m1 = 0.25 * 2610.0 / 4096.0;\n" \
	"    float m2 = 128.0 * 2523.0 / 4096.0;\n" \
	"    float c3 = 32.0 * 2392.0 / 4096.0;\n" \
	"    float c2 = 32.0 * 2413.0 / 4096.0;\n" \
	"    float c1 = c3 - c2 + 1.0;\n" \
	"    vec3 n = pow3(v, 1.0 / m2);\n" \
	"    return pow3(max(n - c1, 0.0) / (c2 - c3 * n), 1.0 / m1);\n" \
	"}\n"

#define ST2084_INVERSE_EOTF \
	"vec3 st2084_inverse_eotf(vec3 l) {\n" \
	"    float m1 = 0.25 * 2610.0 / 4096.0;\n" \
	"    float m2 = 128.0 * 2523.0 / 4096.0;\n" \
	"    float c3 = 32.0 * 2392.0 / 4096.0;\n" \
	"    float c2 = 32.0 * 2413.0 / 4096.0;\n" \
	"    float c1 = c3 - c2 + 1.0;\n" \
	"    vec3 n = pow3(l, m1);\n" \
	"    return pow3((c1 + c2 * n) / (1.0 + c3 * n), m2);\n" \
	"}\n"

#define HLG_OETF \
	"vec3 hlg_oetf(vec3 l) {\n" \
	"    float a = 0.17883277;\n" \
	"    float b = 1.0 - 4.0 * a;\n" \
	"    float c = 0.5 - a * log(4.0 * a);\n" \
	"    float x = step(1.0 / 12.0, l);\n" \
	"    vec3 v0 = a * log(12.0 * l - b) + c;\n" \
	"    vec3 v1 = sqrt(3.0 * l);\n" \
	"    return mix(v0, v1, x);\n" \
	"}\n"

#define HLG_EOTF \
	"vec3 hlg_eotf(vec3 l) {\n" \
	"    float a = 0.17883277;\n" \
	"    float b = 1.0 - 4.0 * a;\n" \
	"    float c = 0.5 - a * log(4.0 * a);\n" \
	"    float x = step(1.0 / 2.0, l);\n" \
	"    vec3 v0 = pow(l, 2.0) / 3.0;\n" \
	"    vec3 v1 = (exp((l - c) / a) + b) / 12.0;\n" \
	"    return mix(v0, v1, x);\n" \
	"}\n"


#endif
