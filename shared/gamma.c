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

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef IN_WESTON
#include <wayland-server.h>
#else
#define WL_EXPORT
#endif

#include "helpers.h"
#include "gamma.h"

static const struct weston_gamma_coeff bt709 = {
	.name = "BT.709",
	.p = 0.45f,
	.a = 0.099f,
	.knee = 0.018f,
	.linear = 4.5f,
};

static const struct weston_gamma_coeff smpte240m = {
	.name = "SMPTE 240M",
	.p = 0.45f,
	.a = 0.1115f,
	.knee = 0.0228f,
	.linear = 4.0f,
};

static const struct weston_gamma_coeff srgb = {
	.name = "sRGB",
	.p = 1.0f / 2.4f,
	.a = 0.055f,
	.knee = 0.04045f / 12.92f,
	.linear = 12.92f,
};

static const struct weston_gamma_coeff adobergb = {
	.name = "AdobeRGB",
	.p = 1.0f / 2.19921875f,
	.linear = 1.0f,
};

static const struct weston_gamma_coeff dci_p3 = {
	.name = "DCI-P3",
	.p = 1.0f / 2.6f,
	.linear = 1.0f,
};

static const struct weston_gamma_coeff prophotorgb = {
	.name = "ProphotoRGB",
	.p = 1.0f / 1.8f,
	.a = 0.0f,
	.knee = 0.001953f,
	.linear = 16.0f,
};

static const struct weston_gamma_coeff st2084 = {
	.name = "ST2084",
};

static const struct weston_gamma_coeff linear = {
	.name = "Linear",
	.p = 1.0f,
	.linear = 1.0f,
};

static const struct weston_gamma_coeff *gamma_coeffs[] = {
	&bt709,
	&smpte240m,
	&srgb,
	&adobergb,
	&dci_p3,
	&prophotorgb,
	&st2084,
	&linear,
};

static float max(float a, float b)
{
	return a > b ? a : b;
}

static float mix(float x, float y, float a)
{
	return x * (1.0f - a) + y * a;
}

static float step(float edge, float x)
{
	return x < edge ? 0.0f : 1.0f;
}

static float degamma(float v, float p, float a, float knee, float linear)
{
	float ls = v / linear;
	float ps = powf((v + a) / (1.0f + a), p);
	return mix(ls, ps, step(knee, v));
}

static float _gamma(float l, float p, float a, float knee, float linear)
{
	float ls = l * linear;
	float ps = (1.0f + a) * powf(l, p) - a;
	return mix(ls, ps, step(knee, l));
}

static float st2084_eotf(float v)
{
	float m1 = 0.25f * 2610.0f / 4096.0f;
	float m2 = 128.0f * 2523.0f / 4096.0f;
	float c3 = 32.0f * 2392.0f / 4096.0f;
	float c2 = 32.0f * 2413.0f / 4096.0f;
	float c1 = c3 - c2 + 1.0f;
	float n = powf(v, 1.0f / m2);
	return powf(max(n - c1, 0.0f) / (c2 - c3 * n), 1.0f / m1);
}

static float st2084_inverse_eotf(float l)
{
	float m1 = 0.25f * 2610.0f / 4096.0f;
	float m2 = 128.0f * 2523.0f / 4096.0f;
	float c3 = 32.0f * 2392.0f / 4096.0f;
	float c2 = 32.0f * 2413.0f / 4096.0f;
	float c1 = c3 - c2 + 1.0f;
	float n = powf(l, m1);
	return powf((c1 + c2 * n) / (1.0f + c3 * n), m2);
}

static float hlg_oetf(float l)
{
	float a = 0.17883277f;
	float b = 1.0f - 4.0f * a;
	float c = 0.5f - a * log(4.0f * a);
	float x = step(1.0f / 12.0f, l);
	float v0 = a * logf(12.0f * l - b) + c;
	float v1 = sqrtf(3.0f * l);
	return mix(v0, v1, x);
}

static float hlg_eotf(float l)
{
	float a = 0.17883277f;
	float b = 1.0f - 4.0f * a;
	float c = 0.5f - a * log(4.0f * a);
	float x = step(1.0f / 2.0f, l);
	float v0 = powf(l, 2.0f) / 3.0f;
	float v1 = (expf((l - c) / a) + b) / 12.0f;
	return mix(v0, v1, x);
}

WL_EXPORT float
weston_gamma(struct weston_gamma_coeff *c, float x)
{
	if (!strcmp(c->name, "ST2084"))
		return st2084_inverse_eotf(x);
	else if (!strcmp(c->name, "HLG"))
		return hlg_oetf(x);
	else
		return _gamma(x, c->p, c->a, c->knee, c->linear);
}

WL_EXPORT float
weston_degamma(struct weston_gamma_coeff *c, float x)
{
	if (!strcmp(c->name, "ST2084"))
		return st2084_eotf(x);
	else if (!strcmp(c->name, "HLG"))
		return hlg_eotf(x);
	else
		return degamma(x, c->p, c->a, c->knee, c->linear);
}

static const struct weston_gamma_coeff *
lookup_gamma(const char *name)
{
	unsigned i;

	for (i = 0; i < ARRAY_LENGTH(gamma_coeffs); i++) {
		const struct weston_gamma_coeff *c = gamma_coeffs[i];

		if (!strcmp(c->name, name))
			return c;
	}

	return NULL;
}

WL_EXPORT void
weston_gamma_lookup(struct weston_gamma_coeff *coeff,
		    const char *name)
{
	const struct weston_gamma_coeff *c = lookup_gamma(name);
	if (!c) {
		memset(coeff, 0, sizeof(*coeff));
		coeff->name = name;
		return;
	}

	*coeff = *c;
}


WL_EXPORT void
weston_degamma_lookup(struct weston_gamma_coeff *coeff,
		      const char *name)
{
	const struct weston_gamma_coeff *c = lookup_gamma(name);
	if (!c) {
		memset(coeff, 0, sizeof(*coeff));
		coeff->name = name;
		return;
	}

	coeff->name = c->name;
	coeff->p = 1.0f / c->p;
	coeff->a = c->a;
	coeff->knee = c->knee * c->linear;
	coeff->linear = c->linear;
}

WL_EXPORT void
weston_gamma_print(const struct weston_gamma_coeff *coeff)
{
	printf("%s %.6f %.6f %.6f %.6f\n",
	       coeff->name, coeff->p, coeff->a,
	       coeff->knee, coeff->linear);
}
