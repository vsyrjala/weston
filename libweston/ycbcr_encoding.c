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

#include <string.h>

#include "compositor.h"
#include "color_encoding.h"

#include "ycbcr-encoding-unstable-v1-server-protocol.h"

static void
ycbcr_encoding_destroy_request(struct wl_client *client,
			   struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
ycbcr_encoding_set_request(struct wl_client *client,
			   struct wl_resource *resource,
			   struct wl_resource *surface_resource,
			   uint32_t encoding,
			   uint32_t quantization)
{
	static const char * const color_encoding_names[] = {
		[ZWP_YCBCR_ENCODING_V1_ENCODING_UNDEFINED] = "BT.601",
		[ZWP_YCBCR_ENCODING_V1_ENCODING_BT601] = "BT.601",
		[ZWP_YCBCR_ENCODING_V1_ENCODING_BT709] = "BT.709",
		[ZWP_YCBCR_ENCODING_V1_ENCODING_SMPTE240M] = "SMPTE 240M",
		[ZWP_YCBCR_ENCODING_V1_ENCODING_BT2020] = "BT.2020",
	};
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	const struct weston_color_encoding *e;

	e = weston_color_encoding_lookup(color_encoding_names[encoding]);
	if (!e)
		return;

	surface->ycbcr_encoding = e;
	surface->ycbcr_full_range =
		quantization == ZWP_YCBCR_ENCODING_V1_QUANTIZATION_FULL;
}

static const struct zwp_ycbcr_encoding_v1_interface
zwp_ycbcr_encoding_implementation = {
	.destroy = ycbcr_encoding_destroy_request,
	.set = ycbcr_encoding_set_request,
};

static void
bind_ycbcr_encoding(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct weston_compositor *compositor = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &zwp_ycbcr_encoding_v1_interface,
				      version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &zwp_ycbcr_encoding_implementation,
				       compositor, NULL);
}

WL_EXPORT int
weston_ycbcr_encoding_setup(struct weston_compositor *compositor)
{
	if (!wl_global_create(compositor->wl_display,
			      &zwp_ycbcr_encoding_v1_interface, 1,
			      compositor, bind_ycbcr_encoding))
		return -1;

	return 0;
}

