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

#ifndef WESTON_GAMMA_H
#define WESTON_GAMMA_H

#include "colorspace.h"
#include "matrix.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct weston_gamma_coeff {
	const char *name;
	float p;
	float a;
	float knee;
	float linear;
};

void weston_gamma_lookup(struct weston_gamma_coeff *coeff,
			 const char *name);
void weston_degamma_lookup(struct weston_gamma_coeff *coeff,
			   const char *name);
void weston_gamma_print(const struct weston_gamma_coeff *coeff);

float weston_gamma(struct weston_gamma_coeff *c, float x);
float weston_degamma(struct weston_gamma_coeff *c, float x);

#endif
