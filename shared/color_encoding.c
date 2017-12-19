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

#include <assert.h>
#include <string.h>

#ifdef IN_WESTON
#include <wayland-server.h>
#else
#define WL_EXPORT
#endif

#include "helpers.h"
#include "matrix.h"
#include "color_encoding.h"

static const struct weston_color_encoding encodings[] = {
	{ .name = "BT.601",     .kr = 0.299f,  .kb = 0.114f,  },
	{ .name = "BT.709",     .kr = 0.2126f, .kb = 0.0722f, },
	{ .name = "SMPTE 240M", .kr = 0.212f,  .kb = 0.087f,  },
	{ .name = "BT.2020",    .kr = 0.2627f, .kb = 0.0593f, },
};

WL_EXPORT const struct weston_color_encoding *
weston_color_encoding_lookup(const char *name)
{
	unsigned i;

	if (!name)
		return NULL;

	for (i = 0; i < ARRAY_LENGTH(encodings); i++) {
		if (!strcmp(name, encodings[i].name))
			return &encodings[i];
	}

	return NULL;
}

static void rgb2ycbcr_matrix(struct weston_matrix *matrix,
			     const struct weston_color_encoding *e)
{
	float kr, kg, kb;

	kr = e->kr;
	kb = e->kb;
	kg = 1.0f - kr - kb;

	weston_matrix_init(matrix);

	matrix->d[0 * 4 + 0] = kr;
	matrix->d[1 * 4 + 0] = kg;
	matrix->d[2 * 4 + 0] = kb;

	matrix->d[0 * 4 + 1] = -0.5f * kr / (1.0f - kb);
	matrix->d[1 * 4 + 1] = -0.5f * kg / (1.0f - kb);
	matrix->d[2 * 4 + 1] = 0.5f;

	matrix->d[0 * 4 + 2] = 0.5f;
	matrix->d[1 * 4 + 2] = -0.5f * kg / (1.0f - kr);
	matrix->d[2 * 4 + 2] = -0.5f * kb / (1.0f - kr);
}

static void
weston_rgb2ycbcr_matrix(struct weston_matrix *matrix,
			const struct weston_color_encoding *e,
			float bpc_mul, bool full_range)
{
	rgb2ycbcr_matrix(matrix, e);

	if (!full_range)
		weston_matrix_scale(matrix,
				    219.0f / 255.0f,
				    112.0f / 128.0f,
				    112.0f / 128.0f);

	weston_matrix_translate(matrix,
				full_range ?
				0.0f : 16.0f / 255.0f,
				0.5f,
				0.5f);

	weston_matrix_scale(matrix,
			    bpc_mul,
			    bpc_mul,
			    bpc_mul);
}

WL_EXPORT void
weston_ycbcr2rgb_matrix(struct weston_matrix *matrix,
			const struct weston_color_encoding *e,
			float bpc_mul, bool full_range)
{
	struct weston_matrix rgb2ycbcr;
	int err;

	weston_rgb2ycbcr_matrix(&rgb2ycbcr, e, 1.0f / bpc_mul, full_range);

	err = weston_matrix_invert(matrix, &rgb2ycbcr);
	assert(err == 0);
}
