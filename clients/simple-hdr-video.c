/*
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010-2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-unstable-v6-client-protocol.h"
#include "colorspace-unstable-v1-client-protocol.h"
#include "ycbcr-encoding-unstable-v1-client-protocol.h"
#include "xdg-shell-unstable-v6-client-protocol.h"
#include "xdg-shell-unstable-v6-client-protocol.h"
#include <sys/types.h>
#include <unistd.h>

#include "shared/os-compatibility.h"
#include "shared/helpers.h"
#include "shared/gamma.h"
#include "shared/platform.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

struct window;

struct buffer_pool {
	struct display *display;
	int fd;
	uint8_t *data;
	size_t size, used;
	int width, height, stride, format;
	AVBufferPool *pool;
	struct wl_shm_pool *shm_pool;
};

struct video {
	AVFormatContext *fmt_ctx;
	AVCodecParserContext *parser;
	AVCodecContext *codec;
	AVPacket *pkt;
	int stream_index;

	struct buffer_pool pool;
};

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct zxdg_shell_v6 *shell;
	struct wl_shm *shm;
	struct window *window;
	struct zwp_colorspace_v1 *colorspace;
	struct zwp_ycbcr_encoding_v1 *ycbcr_encoding;
};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	struct geometry geometry, window_size;
	struct video video;

	struct wl_surface *surface;
	struct zxdg_surface_v6 *xdg_surface;
	struct zxdg_toplevel_v6 *xdg_toplevel;
	struct wl_callback *callback;
	int fullscreen, delay;
	bool wait_for_configure;
};

static int running = 1;

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
	AVFrame *frame = data;

	av_frame_free(&frame);
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static uint32_t av_format_to_wl_format(int format)
{
	switch (format) {
	case AV_PIX_FMT_YUV420P:
		return WL_SHM_FORMAT_YUV420;
	case AV_PIX_FMT_YUV420P10BE:
	case AV_PIX_FMT_YUV420P10LE:
		return WL_SHM_FORMAT_YUV420_10;
	case AV_PIX_FMT_YUV420P12BE:
	case AV_PIX_FMT_YUV420P12LE:
		return WL_SHM_FORMAT_YUV420_12;
	case AV_PIX_FMT_YUV420P16BE:
	case AV_PIX_FMT_YUV420P16LE:
		return WL_SHM_FORMAT_YUV420_16;
	default:
		return -1;
	}
}

static bool
decode(struct video *s, AVFrame *frame)
{
	int r;

	if (s->pkt->size == 0)
		return false;

	if (s->pkt->stream_index != s->stream_index)
		return false;

	r = avcodec_send_packet(s->codec, s->pkt);
	if (r < 0)
		return false;

	r = avcodec_receive_frame(s->codec, frame);
	if (r < 0)
		return false;

	return frame->opaque;
}

static AVFrame *
demux_and_decode(struct window *w, struct video *s)
{
	AVFrame *frame;
	bool ret;

	frame = av_frame_alloc();
	if (!frame)
		return NULL;

	for (;;) {
		int r;

		r = av_read_frame(s->fmt_ctx, s->pkt);
		if (r < 0)
			break;

		ret = decode(s, frame);

		av_packet_unref(s->pkt);

		if (ret)
			break;
	}

	if (!ret)
		av_frame_free(&frame);

	return frame;
}

static void video_close(struct video *s)
{
	av_parser_close(s->parser);
	avcodec_free_context(&s->codec);
	av_packet_free(&s->pkt);
}

static enum zwp_ycbcr_encoding_v1_quantization video_quant_range(struct video *s)
{
	if (s->codec->color_range != AVCOL_RANGE_JPEG)
		return ZWP_YCBCR_ENCODING_V1_QUANTIZATION_LIMITED;
	else
		return ZWP_YCBCR_ENCODING_V1_QUANTIZATION_FULL;
}

static const enum zwp_ycbcr_encoding_v1_encoding color_encodings[] = {
	[AVCOL_SPC_BT709] = ZWP_YCBCR_ENCODING_V1_ENCODING_BT709,
	[AVCOL_SPC_BT470BG] = ZWP_YCBCR_ENCODING_V1_ENCODING_BT601,
	[AVCOL_SPC_SMPTE170M] = ZWP_YCBCR_ENCODING_V1_ENCODING_BT601,
	[AVCOL_SPC_SMPTE240M] = ZWP_YCBCR_ENCODING_V1_ENCODING_SMPTE240M,
	[AVCOL_SPC_BT2020_CL] = ZWP_YCBCR_ENCODING_V1_ENCODING_BT2020,
	[AVCOL_SPC_BT2020_NCL] = ZWP_YCBCR_ENCODING_V1_ENCODING_BT2020,
};

static enum zwp_ycbcr_encoding_v1_encoding
video_color_encoding(struct video *s)
{
	if (s->codec->colorspace >= ARRAY_LENGTH(color_encodings))
		return ZWP_YCBCR_ENCODING_V1_ENCODING_BT601;

	return color_encodings[s->codec->colorspace];
}

static const enum zwp_colorspace_v1_chromacities chromacities[] = {
	[AVCOL_PRI_BT709] = ZWP_COLORSPACE_V1_CHROMACITIES_BT709,
	[AVCOL_PRI_BT470M] = ZWP_COLORSPACE_V1_CHROMACITIES_BT470M,
	[AVCOL_PRI_BT470BG] = ZWP_COLORSPACE_V1_CHROMACITIES_BT470BG,
	[AVCOL_PRI_SMPTE170M] = ZWP_COLORSPACE_V1_CHROMACITIES_SMPTE170M,
	[AVCOL_PRI_SMPTE240M] = ZWP_COLORSPACE_V1_CHROMACITIES_SMPTE170M,
	[AVCOL_PRI_SMPTE431] = ZWP_COLORSPACE_V1_CHROMACITIES_DCI_P3,
	[AVCOL_PRI_SMPTE432] = ZWP_COLORSPACE_V1_CHROMACITIES_DCI_P3,
	[AVCOL_PRI_SMPTE428] = ZWP_COLORSPACE_V1_CHROMACITIES_CIEXYZ,
	[AVCOL_PRI_BT2020] = ZWP_COLORSPACE_V1_CHROMACITIES_BT2020,
};

static enum zwp_colorspace_v1_chromacities
video_chromacities(struct video *s)
{
	if (s->codec->color_primaries >= ARRAY_LENGTH(chromacities))
		return ZWP_COLORSPACE_V1_CHROMACITIES_UNDEFINED;

	return chromacities[s->codec->color_primaries];
}

static const enum zwp_colorspace_v1_transfer_func transfer_funcs[] = {
	[AVCOL_TRC_BT709] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_BT709,
	[AVCOL_TRC_GAMMA22] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_BT709,
	[AVCOL_TRC_GAMMA28] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_BT709,
	[AVCOL_TRC_SMPTE170M] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_BT709,
	[AVCOL_TRC_SMPTE240M] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_SMPTE240M,
	[AVCOL_TRC_BT2020_10] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_BT709,
	[AVCOL_TRC_BT2020_12] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_BT709,
	[AVCOL_TRC_SMPTE2084] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_ST2084,
	[AVCOL_TRC_ARIB_STD_B67] = ZWP_COLORSPACE_V1_TRANSFER_FUNC_HLG,
};

static enum zwp_colorspace_v1_transfer_func
video_transfer_func(struct video *s)
{
	if (s->codec->color_trc >= ARRAY_LENGTH(transfer_funcs))
		return ZWP_COLORSPACE_V1_TRANSFER_FUNC_LINEAR;

	return transfer_funcs[s->codec->color_trc];
}

static void buffer_free(void *opaque, uint8_t *data)
{
}

static AVBufferRef *pool_alloc(void *opaque, int size)
{
	struct buffer_pool *pool = opaque;
	uint8_t *data;

	assert((size_t)size <= pool->size - pool->used);

	data = pool->data + pool->used;
	pool->used += size;

	return av_buffer_create(data, size, buffer_free, NULL, 0);
}

#define ALIGN(x, a) (((x)+(a)-1)&~((a)-1))

static void pool_update(struct buffer_pool *pool,
			AVFrame *frame, unsigned int size)
{
	/* 8 frames maybe? */
	unsigned int pool_size = 8 * size;

	if (pool->format == frame->format &&
	    pool->width == frame->width &&
	    pool->height == frame->height &&
	    pool->stride == frame->linesize[0] &&
	    pool->size - pool->used >= size)
		return;

	if (pool->pool) {
		munmap(pool->data, pool->size);
		close(pool->fd);
		av_buffer_pool_uninit(&pool->pool);
	}


	pool->fd = os_create_anonymous_file(pool_size);
	if (pool->fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
			pool_size);
		return;
	}

	pool->data = mmap(NULL, pool_size,
			  PROT_READ | PROT_WRITE, MAP_SHARED,
			  pool->fd, 0);
	if (pool->data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		return;
	}
	if (!pool->shm_pool)
		pool->shm_pool = wl_shm_create_pool(pool->display->shm,
						    pool->fd, pool_size);
	else
		wl_shm_pool_resize(pool->shm_pool, pool_size);

	pool->pool = av_buffer_pool_init2(size, pool,
					  pool_alloc, NULL);

	pool->format = frame->format;
	pool->width  = frame->width;
	pool->height = frame->height;
	pool->stride = frame->linesize[0];
	pool->size = pool_size;
	pool->used = 0;
}

static int video_get_buffer2(struct AVCodecContext *codec, AVFrame *frame, int flags)
{
	struct buffer_pool *pool = codec->opaque;
	int width[3] = { frame->width, frame->width/2, frame->width/2 };
	int height[3] = { frame->height, frame->height/2, frame->height/2 };
	int size[3], total_size, cpp;

	(void)flags;

	switch (frame->format) {
	case AV_PIX_FMT_YUV420P:
		cpp = 1;
		break;
	case AV_PIX_FMT_YUV420P10BE:
	case AV_PIX_FMT_YUV420P10LE:
		cpp = 2;
		break;
	case AV_PIX_FMT_YUV420P12BE:
	case AV_PIX_FMT_YUV420P12LE:
		cpp = 2;
		break;
	case AV_PIX_FMT_YUV420P16BE:
	case AV_PIX_FMT_YUV420P16LE:
		cpp = 2;
		break;
	default:
		fprintf(stderr, "unknown format %x\n", frame->format);
		return AVERROR(ENOMEM);
	}

	frame->linesize[0] = ALIGN(width[0] * cpp, 64);
	frame->linesize[1] = frame->linesize[0] / 2;
	frame->linesize[2] = frame->linesize[0] / 2;

	size[0] = frame->linesize[0] * height[0];
	size[1] = frame->linesize[1] * height[1];
	size[2] = frame->linesize[2] * height[2];

	total_size = size[0] + size[1] + size[2];

	pool_update(pool, frame, total_size);

	memset(frame->data, 0, sizeof(frame->data));
	frame->extended_data = frame->data;

	frame->buf[0] = av_buffer_pool_get(pool->pool);

	frame->opaque = (void*)true;

	assert(frame->buf[0]->size >= total_size);

	frame->data[0] = frame->buf[0]->data;
	frame->data[1] = frame->data[0] + size[0];
	frame->data[2] = frame->data[1] + size[1];

	return 0;
}

static bool video_open(struct display *display,
		       struct video *s,
		       const char *filename)
{
	AVCodec *codec = NULL;
	AVStream *stream;
	int r;
	char buf[4096] = {};

	av_register_all();

	r = avformat_open_input(&s->fmt_ctx, filename, NULL, NULL);
	if (r < 0)
		return false;

	r = avformat_find_stream_info(s->fmt_ctx, NULL);
	if (r < 0)
		return false;

	r = av_find_best_stream(s->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (r < 0)
		return false;

	stream = s->fmt_ctx->streams[r];
	s->stream_index = r;

	s->codec = avcodec_alloc_context3(codec);
	if (!s->codec)
		return false;

	s->codec->get_buffer2 = video_get_buffer2;
	s->pool.display = display;
	s->codec->opaque = &s->pool;

	r = avcodec_parameters_to_context(s->codec, stream->codecpar);
	if (r < 0)
		return false;

	r = avcodec_open2(s->codec, codec, NULL);
	if (r < 0)
		return false;

	s->parser = av_parser_init(codec->id);
	if (!s->parser)
		return false;

	avcodec_string(buf, sizeof(buf), s->codec, false);
	buf[sizeof(buf)-1] = '\0';
	puts(buf);

	s->pkt = av_packet_alloc();

	return true;
}
static void
redraw(void *data, struct wl_callback *callback, uint32_t time);

static void
handle_surface_configure(void *data, struct zxdg_surface_v6 *surface,
			 uint32_t serial)
{
	struct window *window = data;

	zxdg_surface_v6_ack_configure(surface, serial);

	if (window->wait_for_configure) {
		redraw(window, NULL, 0);
		window->wait_for_configure = false;
	}
}

static const struct zxdg_surface_v6_listener xdg_surface_listener = {
	handle_surface_configure
};

static void
handle_toplevel_configure(void *data, struct zxdg_toplevel_v6 *toplevel,
			  int32_t width, int32_t height,
			  struct wl_array *states)
{
	struct window *window = data;
	uint32_t *p;

	window->fullscreen = 0;
	wl_array_for_each(p, states) {
		uint32_t state = *p;
		switch (state) {
		case ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN:
			window->fullscreen = 1;
			break;
		}
	}

	if (width > 0 && height > 0) {
		if (!window->fullscreen) {
			window->window_size.width = width;
			window->window_size.height = height;
		}
		window->geometry.width = width;
		window->geometry.height = height;
	} else if (!window->fullscreen) {
		window->geometry = window->window_size;
	}
}

static void
handle_toplevel_close(void *data, struct zxdg_toplevel_v6 *xdg_toplevel)
{
	running = 0;
}

static const struct zxdg_toplevel_v6_listener xdg_toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close,
};

static void
create_xdg_surface(struct window *window, struct display *display)
{
	window->xdg_surface = zxdg_shell_v6_get_xdg_surface(display->shell,
							    window->surface);
	zxdg_surface_v6_add_listener(window->xdg_surface,
				     &xdg_surface_listener, window);

	window->xdg_toplevel =
		zxdg_surface_v6_get_toplevel(window->xdg_surface);
	zxdg_toplevel_v6_add_listener(window->xdg_toplevel,
				      &xdg_toplevel_listener, window);

	zxdg_toplevel_v6_set_title(window->xdg_toplevel, "simple-egl");

	window->wait_for_configure = true;
	wl_surface_commit(window->surface);
}

static void
create_surface(struct window *window)
{
	struct display *display = window->display;
	struct video *video = &window->video;

	window->surface = wl_compositor_create_surface(display->compositor);

	create_xdg_surface(window, display);

	if (window->fullscreen)
		zxdg_toplevel_v6_set_fullscreen(window->xdg_toplevel, NULL);

	zwp_colorspace_v1_set(display->colorspace,
			      window->surface,
			      video_chromacities(video),
			      video_transfer_func(video));

	zwp_ycbcr_encoding_v1_set(display->ycbcr_encoding,
				  window->surface,
				  video_color_encoding(video),
				  video_quant_range(video));
}

static void
destroy_surface(struct window *window)
{
	if (window->xdg_toplevel)
		zxdg_toplevel_v6_destroy(window->xdg_toplevel);
	if (window->xdg_surface)
		zxdg_surface_v6_destroy(window->xdg_surface);
	wl_surface_destroy(window->surface);

	if (window->callback)
		wl_callback_destroy(window->callback);
}

static const struct wl_callback_listener frame_listener;

static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	struct video *video = &window->video;
	struct buffer_pool *pool = &video->pool;
	struct wl_region *region;
	struct wl_buffer *buffer;
	uint32_t format;
	AVFrame *frame;

	assert(window->callback == callback);
	window->callback = NULL;

	if (callback)
		wl_callback_destroy(callback);

	usleep(window->delay);

	if (window->fullscreen) {
		region = wl_compositor_create_region(window->display->compositor);
		wl_region_add(region, 0, 0,
			      window->geometry.width,
			      window->geometry.height);
		wl_region_destroy(region);
	}

	frame = demux_and_decode(window, &window->video);
	if (!frame) {
		fprintf(stderr, "no more frames?\n");
		return;
	}

	format = av_format_to_wl_format(frame->format);

	assert(frame->data[0] >= pool->data &&
	       frame->data[0] < pool->data + pool->size);

	buffer = wl_shm_pool_create_buffer(pool->shm_pool,
					   frame->data[0] - pool->data,
					   frame->width, frame->height,
					   frame->linesize[0], format);

	wl_buffer_add_listener(buffer, &buffer_listener, frame);

	wl_surface_attach(window->surface, buffer, 0, 0);
	wl_surface_damage(window->surface, 0, 0, frame->width, frame->height);

	window->callback = wl_surface_frame(window->surface);
	wl_callback_add_listener(window->callback, &frame_listener, window);
	wl_surface_commit(window->surface);
}

static const struct wl_callback_listener frame_listener = {
	redraw
};

static void
xdg_shell_ping(void *data, struct zxdg_shell_v6 *shell, uint32_t serial)
{
	zxdg_shell_v6_pong(shell, serial);
}

static const struct zxdg_shell_v6_listener xdg_shell_listener = {
	xdg_shell_ping,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t name, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_shm") == 0) {
		d->shm = wl_registry_bind(registry,
					  name, &wl_shm_interface, 1);
	} else if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface,
					 MIN(version, 4));
	} else if (strcmp(interface, "zxdg_shell_v6") == 0) {
		d->shell = wl_registry_bind(registry, name,
					    &zxdg_shell_v6_interface, 1);
		zxdg_shell_v6_add_listener(d->shell, &xdg_shell_listener, d);
	} else if (strcmp(interface, "zwp_colorspace_v1") == 0) {
		d->colorspace = wl_registry_bind(registry, name,
						 &zwp_colorspace_v1_interface, 1);
	} else if (strcmp(interface, "zwp_ycbcr_encoding_v1") == 0) {
		d->ycbcr_encoding = wl_registry_bind(registry, name,
						     &zwp_ycbcr_encoding_v1_interface, 1);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void
signal_int(int signum)
{
	running = 0;
}

static void
usage(int error_code)
{
	fprintf(stderr, "Usage: simple-egl [OPTIONS]\n\n"
		"  -d <us>\tBuffer swap delay in microseconds\n"
		"  -f\tRun in fullscreen mode\n"
		"  -b\tDon't sync to compositor redraw (eglSwapInterval 0)\n"
		"  -h\tThis help text\n\n");

	exit(error_code);
}

int
main(int argc, char **argv)
{
	struct sigaction sigint;
	struct display display = { 0 };
	struct window  window  = { 0 };
	int i, ret = 0;

	window.display = &display;
	display.window = &window;
	window.geometry.width  = 250;
	window.geometry.height = 250;
	window.window_size = window.geometry;
	window.delay = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp("-d", argv[i]) == 0 && i+1 < argc)
			window.delay = atoi(argv[++i]);
		else if (strcmp("-f", argv[i]) == 0)
			window.fullscreen = 1;
		else if (strcmp("-h", argv[i]) == 0)
			usage(EXIT_SUCCESS);
		else
			break;
	}

	display.display = wl_display_connect(NULL);
	assert(display.display);

	display.registry = wl_display_get_registry(display.display);
	wl_registry_add_listener(display.registry,
				 &registry_listener, &display);

	wl_display_roundtrip(display.display);

	if (!video_open(&display, &window.video, argv[i]))
		usage(EXIT_FAILURE);

	create_surface(&window);

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	while (running && ret != -1)
		wl_display_dispatch(display.display);

	fprintf(stderr, "simple-egl exiting\n");

	destroy_surface(&window);

	video_close(&window.video);

	if (display.shm)
		wl_shm_destroy(display.shm);
	if (display.shell)
		zxdg_shell_v6_destroy(display.shell);
	if (display.compositor)
		wl_compositor_destroy(display.compositor);

	if (display.colorspace)
		zwp_colorspace_v1_destroy(display.colorspace);
	if (display.ycbcr_encoding)
		zwp_ycbcr_encoding_v1_destroy(display.ycbcr_encoding);

	wl_registry_destroy(display.registry);
	wl_display_flush(display.display);
	wl_display_disconnect(display.display);

	return 0;
}
