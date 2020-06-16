/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
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

#ifndef INCLUDE_VIDEO_H
#define INCLUDE_VIDEO_H

#include "common.h"

#ifndef V4L2_PIX_FMT_NV12_UBWC
/* Qualcomm UBWC 8-bit Y/CbCr 4:2:0 */
#define V4L2_PIX_FMT_NV12_UBWC	v4l2_fourcc('Q', '1', '2', '8')
#endif

int video_open(struct video *vid, const char *name);
void video_close(struct video *vid);
int video_setup_output(struct video *vid, unsigned int size, unsigned int count);
int video_gfmt_cap(struct video *vid, unsigned int *width, unsigned int *height,
		   unsigned int *sizeimage);
int video_sfmt_cap(struct video *vid, unsigned int width, unsigned int height,
		   unsigned int pixfmt, unsigned int *sizeimage);
int video_sfmt_out(struct video *vid, unsigned int width, unsigned int height,
		   unsigned int pixfmt);
int video_setup_capture(struct video *vid, unsigned int extra_buf,
			unsigned int w, unsigned int h);
int video_qbuf_out(struct video *vid, unsigned int n, unsigned int length);
int video_qbuf_cap(struct video *vid, unsigned int n);
int video_qbuf_cap_dmabuf(struct video *vid, unsigned int index, int dbuf_fd);
int video_dqbuf_out(struct video *vid, unsigned int *n);
int video_dqbuf_cap(struct video *vid, unsigned int *n, unsigned int *finished,
		    unsigned int *bytesused, unsigned int *sequence);
int video_dqbuf_cap_dmabuf(struct video *vid, unsigned int *n,
			   unsigned int *finished, unsigned int *bytesused);
int video_streamon_cap(struct video *vid);
int video_streamon_out(struct video *vid);
int video_streamoff_cap(struct video *vid);
int video_streamoff_out(struct video *vid);

int video_buf_exp(struct video *vid, unsigned int index);
int video_buf_create(struct video *vid, unsigned int width,
		     unsigned int height,unsigned int *index,
		     unsigned int count);
int video_set_control(struct video *vid);
int video_set_framerate(struct video *vid, unsigned int framerate);
int video_stop(struct video *vid);
int video_dec_stop(struct video *vid);
int video_dec_start(struct video *vid);

int video_event_subscribe(struct video *vid, unsigned int event_type);
int video_event_unsubscribe(struct video *vid, unsigned int event_type);
int video_event_dequeue(struct video *vid, struct v4l2_event *ev);

#endif /* INCLUDE_VIDEO_H */
