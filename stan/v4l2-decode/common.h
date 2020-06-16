/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Common stuff header file
 *
 * Copyright 2012 Samsung Electronics Co., Ltd.
 * Copyright (c) 2015 Linaro Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef INCLUDE_COMMON_H
#define INCLUDE_COMMON_H

#include <stdio.h>
#include <semaphore.h>
#include <sys/time.h>
#include <stdint.h>

#include "parser.h"

#ifndef V4L2_PIX_FMT_HEVC
#define V4L2_PIX_FMT_HEVC	v4l2_fourcc('H', 'E', 'V', 'C') /* HEVC */
#endif

#ifndef V4L2_PIX_FMT_VP9
#define V4L2_PIX_FMT_VP9	v4l2_fourcc('V', 'P', '9', '0') /* VP9 */
#endif

/* When ADD_DETAILS is defined every debug and error message contains
 * information about the file, function and line of code where it has
 * been called */
#define ADD_DETAILS

/* When DEBUG is defined debug messages are printed on the screen.
 * Otherwise only error messages are displayed. */
//#define DEBUG

#ifdef ADD_DETAILS
#define err(msg, ...) \
	fprintf(stderr, "Error (%s:%s:%d): " msg "\n", __FILE__, \
		__func__, __LINE__, ##__VA_ARGS__)
#else
#define err(msg, ...) \
	fprintf(stderr, "Error: " msg "\n", __FILE__, ##__VA_ARGS__)
#endif /* ADD_DETAILS */

#define info(msg, ...) \
	fprintf(stderr, "Info : " msg "\n", ##__VA_ARGS__)

#ifdef DEBUG
#ifdef ADD_DETAILS
#define dbg(msg, ...) \
	fprintf(stdout, "(%s:%s:%d): " msg "\n", __FILE__, \
		__func__, __LINE__, ##__VA_ARGS__)
#else
#define dbg(msg, ...) \
	fprintf(stdout, msg "\n", ##__VA_ARGS__)
#endif /* ADD_DETAILS */
#else /* DEBUG */
#define dbg(...) {}
#endif /* DEBUG */

#define memzero(x)	memset(&(x), 0, sizeof (x));

/* Maximum number of output buffers */
#define MAX_OUT_BUF		16

/* Maximum number of capture buffers (32 is the limit imposed by MFC */
#define MAX_CAP_BUF		32

/* Number of output planes */
#define OUT_PLANES		1

/* Number of capture planes */
#define CAP_PLANES		1

/* Input file related parameters */
struct input {
	const char *file_name;
	int fd;
	char *p;
	int size;
	int offs;
};

/* video decoder related parameters */
struct video {
	const char *name;
	int fd;

	/* Output queue related */
	unsigned int out_buf_cnt;
	unsigned int out_buf_size;
	unsigned int out_buf_off[MAX_OUT_BUF];
	char *out_buf_addr[MAX_OUT_BUF];
	unsigned int out_buf_flag[MAX_OUT_BUF];

	/* Capture queue related */
	unsigned int cap_w;
	unsigned int cap_h;
	unsigned int cap_crop_w;
	unsigned int cap_crop_h;
	unsigned int cap_crop_left;
	unsigned int cap_crop_top;
	unsigned int cap_buf_cnt;
	unsigned int cap_buf_cnt_min;
	unsigned int cap_buf_size[CAP_PLANES];
	unsigned int cap_buf_off[MAX_CAP_BUF][CAP_PLANES];
	char *cap_buf_addr[MAX_CAP_BUF][CAP_PLANES];
	unsigned int cap_buf_flag[MAX_CAP_BUF];
	unsigned int cap_min_bufs;
	int cap_use_dmabuf;

	unsigned long total_captured;
};

struct buf_stats {
	unsigned int qbuf_counter;
	unsigned int dqbuf_counter;
	struct timeval qbuf_start;
	struct timeval qbuf_end;
	struct timeval dqbuf_start;
	struct timeval dqbuf_end;
	struct timeval start;
	struct timeval end;
};

struct instance {
	int width;
	int height;
	int save_frames;
	char *save_path;

	unsigned int codec;
	const char *file_name;
	struct video video;
	struct parser_context *parser_ctx;
	pthread_mutex_t lock;
	pthread_condattr_t attr;
	pthread_cond_t cond;
	int error;   /* The error flag */
	unsigned int finish;  /* Flag set when decoding has been completed
				and all threads finish */
	/* buffer statistics */
	struct buf_stats *stats;
};

#endif /* INCLUDE_COMMON_H */
