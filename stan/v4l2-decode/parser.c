/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Really simple stream parser file
 *
 * Copyright 2012 Samsung Electronics Co., Ltd.
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

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include "common.h"
#include "parser.h"

#ifndef V4L2_PIX_FMT_VP9
#define V4L2_PIX_FMT_VP9	v4l2_fourcc('V', 'P', '9', '0')
#endif

#ifndef V4L2_PIX_FMT_HEVC
#define V4L2_PIX_FMT_HEVC	v4l2_fourcc('H', 'E', 'V', 'C')
#endif

#define DBG_TAG "parser"

static void *tmp_buffer = NULL;

int parser_mpeg2(struct parser_context *ctx,
		 char *in, int in_size,
		 char *out, int out_size,
		 int *consumed, int *frame_size, int get_head)
{
	char *in_orig;
	char frame_finished;
	int frame_length;

	in_orig = in;

	*consumed = 0;

	frame_finished = 0;

	while (in_size-- > 0) {
		switch (ctx->state) {
		case MPEG4_PARSER_NO_CODE:
			if (*in == 0x0) {
				ctx->state = MPEG4_PARSER_CODE_0x1;
				ctx->tmp_code_start = *consumed;
			}
			break;
		case MPEG4_PARSER_CODE_0x1:
			if (*in == 0x0)
				ctx->state = MPEG4_PARSER_CODE_0x2;
			else
				ctx->state = MPEG4_PARSER_NO_CODE;
			break;
		case MPEG4_PARSER_CODE_0x2:
			if (*in == 0x1) {
				ctx->state = MPEG4_PARSER_CODE_1x1;
			} else if (*in == 0x0) {
				/* We still have two zeroes */
				ctx->tmp_code_start++;
				// TODO XXX check in h264 and mpeg4
			} else {
				ctx->state = MPEG4_PARSER_NO_CODE;
			}
			break;
		case MPEG4_PARSER_CODE_1x1:
			if (*in == 0xb3 || *in == 0xb8) {
				ctx->state = MPEG4_PARSER_NO_CODE;
				ctx->last_tag = MPEG4_TAG_HEAD;
				ctx->headers_count++;
				dbg("Found header at %d (%x)", *consumed, *consumed);
			} else if (*in == 0x00) {
				ctx->state = MPEG4_PARSER_NO_CODE;
				ctx->last_tag = MPEG4_TAG_VOP;
				ctx->main_count++;
				dbg("Found picture at %d (%x)", *consumed, *consumed);
			} else
				ctx->state = MPEG4_PARSER_NO_CODE;
			break;
		}

		if (get_head == 1 && ctx->headers_count >= 1 && ctx->main_count == 1) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			break;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 1 && ctx->main_count == 0) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 0 && ctx->main_count == 1) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 0 && ctx->headers_count > 0 && ctx->main_count == 1) {
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 1 && (ctx->headers_count > 0 || ctx->main_count > 0)) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			if (ctx->headers_count == 0)
				ctx->seek_end = 1;
			else
				ctx->seek_end = 0;
			break;
		}

		in++;
		(*consumed)++;
	}

	*frame_size = 0;

	if (ctx->got_end == 1) {
		frame_length = ctx->code_end;
	} else
		frame_length = *consumed;


	if (ctx->code_start >= 0) {
		frame_length -= ctx->code_start;
		in = in_orig + ctx->code_start;
	} else {
		memcpy(out, ctx->bytes, -ctx->code_start);
		*frame_size += -ctx->code_start;
		out += -ctx->code_start;
		in_size -= -ctx->code_start;
		in = in_orig;
	}

	if (ctx->got_start) {
		if (out_size < frame_length) {
			err("Output buffer too small for current frame (%d < %d)",
				out_size, frame_length);
			return 0;
		}

		memcpy(out, in, frame_length);
		*frame_size += frame_length;

		if (ctx->got_end) {
			ctx->code_start = ctx->code_end - *consumed;
			ctx->got_start = 1;
			ctx->got_end = 0;
			frame_finished = 1;
			if (ctx->last_tag == MPEG4_TAG_VOP) {
				ctx->seek_end = 1;
				ctx->main_count = 0;
				ctx->headers_count = 0;
			} else {
				ctx->seek_end = 0;
				ctx->main_count = 0;
				ctx->headers_count = 1;
			}
			memcpy(ctx->bytes, in_orig + ctx->code_end, *consumed - ctx->code_end);
		} else {
			ctx->code_start = 0;
			frame_finished = 0;
		}
	}

	ctx->tmp_code_start -= *consumed;

	return frame_finished;
}

int parser_mpeg4(struct parser_context *ctx,
		 char *in, int in_size,
		 char *out, int out_size,
		 int *consumed, int *frame_size, int get_head)
{
	char *in_orig;
	char tmp;
	char frame_finished;
	int frame_length;

	in_orig = in;

	*consumed = 0;

	frame_finished = 0;

	while (in_size-- > 0) {
		switch (ctx->state) {
		case MPEG4_PARSER_NO_CODE:
			if (*in == 0x0) {
				ctx->state = MPEG4_PARSER_CODE_0x1;
				ctx->tmp_code_start = *consumed;
			}
			break;
		case MPEG4_PARSER_CODE_0x1:
			if (*in == 0x0)
				ctx->state = MPEG4_PARSER_CODE_0x2;
			else
				ctx->state = MPEG4_PARSER_NO_CODE;
			break;
		case MPEG4_PARSER_CODE_0x2:
			if (*in == 0x1) {
				ctx->state = MPEG4_PARSER_CODE_1x1;
			} else if ((*in & 0xFC) == 0x80) {
				/* Short header */
				ctx->state = MPEG4_PARSER_NO_CODE;
				/* Ignore the short header if the current hasn't
				 * been started with a short header. */

				if (get_head && !ctx->short_header) {
					ctx->last_tag = MPEG4_TAG_HEAD;
					ctx->headers_count++;
					ctx->short_header = 1;
				} else if (!ctx->seek_end ||
					(ctx->seek_end && ctx->short_header)) {
					ctx->last_tag = MPEG4_TAG_VOP;
					ctx->main_count++;
					ctx->short_header = 1;
				}
			} else if (*in == 0x0) {
				ctx->tmp_code_start++;
			} else {
				ctx->state = MPEG4_PARSER_NO_CODE;
			}
			break;
		case MPEG4_PARSER_CODE_1x1:
			tmp = *in & 0xF0;
			if (tmp == 0x00 || tmp == 0x01 || tmp == 0x20 ||
				*in == 0xb0 || *in == 0xb2 || *in == 0xb3 ||
				*in == 0xb5) {
				ctx->state = MPEG4_PARSER_NO_CODE;
				ctx->last_tag = MPEG4_TAG_HEAD;
				ctx->headers_count++;
			} else if (*in == 0xb6) {
				ctx->state = MPEG4_PARSER_NO_CODE;
				ctx->last_tag = MPEG4_TAG_VOP;
				ctx->main_count++;
			} else
				ctx->state = MPEG4_PARSER_NO_CODE;
			break;
		}

		if (get_head == 1 && ctx->headers_count >= 1 && ctx->main_count == 1) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			break;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 1 && ctx->main_count == 0) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 0 && ctx->main_count == 1) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 0 && ctx->headers_count > 0 && ctx->main_count == 1) {
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 1 && (ctx->headers_count > 0 || ctx->main_count > 0)) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			if (ctx->headers_count == 0)
				ctx->seek_end = 1;
			else
				ctx->seek_end = 0;
			break;
		}

		in++;
		(*consumed)++;
	}


	*frame_size = 0;

	if (ctx->got_end == 1) {
		frame_length = ctx->code_end;
	} else
		frame_length = *consumed;


	if (ctx->code_start >= 0) {
		frame_length -= ctx->code_start;
		in = in_orig + ctx->code_start;
	} else {
		memcpy(out, ctx->bytes, -ctx->code_start);
		*frame_size += -ctx->code_start;
		out += -ctx->code_start;
		in_size -= -ctx->code_start;
		in = in_orig;
	}

	if (ctx->got_start) {
		if (out_size < frame_length) {
			err("Output buffer too small for current frame");
			return 0;
		}

		memcpy(out, in, frame_length);
		*frame_size += frame_length;

		if (ctx->got_end) {
			ctx->code_start = ctx->code_end - *consumed;
			ctx->got_start = 1;
			ctx->got_end = 0;
			frame_finished = 1;
			if (ctx->last_tag == MPEG4_TAG_VOP) {
				ctx->seek_end = 1;
				ctx->main_count = 0;
				ctx->headers_count = 0;
			} else {
				ctx->seek_end = 0;
				ctx->main_count = 0;
				ctx->headers_count = 1;
				ctx->short_header = 0;
				/* If the last frame used the short then
				 * we shall save this information, otherwise
				 * it is necessary to clear it */
			}
			memcpy(ctx->bytes, in_orig + ctx->code_end, *consumed - ctx->code_end);
		} else {
			ctx->code_start = 0;
			frame_finished = 0;
		}
	}

	ctx->tmp_code_start -= *consumed;

	return frame_finished;
}

int parser_h264(struct parser_context *ctx,
		char *in, int in_size,
		char *out, int out_size,
		int *consumed, int *frame_size, int get_head)
{
	char *in_orig;
	char tmp;
	char frame_finished;
	int frame_length;

	in_orig = in;

	*consumed = 0;

	frame_finished = 0;

	while (in_size-- > 0) {
		switch (ctx->state) {
		case H264_PARSER_NO_CODE:
			if (*in == 0x0) {
				ctx->state = H264_PARSER_CODE_0x1;
				ctx->tmp_code_start = *consumed;
			}
			break;
		case H264_PARSER_CODE_0x1:
			if (*in == 0x0)
				ctx->state = H264_PARSER_CODE_0x2;
			else
				ctx->state = H264_PARSER_NO_CODE;
			break;
		case H264_PARSER_CODE_0x2:
			if (*in == 0x1) {
				ctx->state = H264_PARSER_CODE_1x1;
			} else if (*in == 0x0) {
				ctx->state = H264_PARSER_CODE_0x3;
			} else {
				ctx->state = H264_PARSER_NO_CODE;
			}
			break;
		case H264_PARSER_CODE_0x3:
			if (*in == 0x1)
				ctx->state = H264_PARSER_CODE_1x1;
			else if (*in == 0x0)
				ctx->tmp_code_start++;
			else
				ctx->state = H264_PARSER_NO_CODE;
			break;
		case H264_PARSER_CODE_1x1:
			tmp = *in & 0x1F;

			if (tmp == 1 || tmp == 5) {
				ctx->state = H264_PARSER_CODE_SLICE;
			} else if (tmp == 6 || tmp == 7 || tmp == 8) {
				ctx->state = H264_PARSER_NO_CODE;
				ctx->last_tag = H264_TAG_HEAD;
				ctx->headers_count++;
			}
			else
				ctx->state = H264_PARSER_NO_CODE;
			break;
		case H264_PARSER_CODE_SLICE:
			if ((*in & 0x80) == 0x80) {
				ctx->main_count++;
				ctx->last_tag = H264_TAG_SLICE;
			}
			ctx->state = H264_PARSER_NO_CODE;
			break;
		}

		if (get_head == 1 && ctx->headers_count >= 1 && ctx->main_count == 1) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			break;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 1 && ctx->main_count == 0) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
		}

		if (ctx->got_start == 0 && ctx->headers_count == 0 && ctx->main_count == 1) {
			ctx->code_start = ctx->tmp_code_start;
			ctx->got_start = 1;
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 0 && ctx->headers_count > 0 && ctx->main_count == 1) {
			ctx->seek_end = 1;
			ctx->headers_count = 0;
			ctx->main_count = 0;
		}

		if (ctx->seek_end == 1 && (ctx->headers_count > 0 || ctx->main_count > 0)) {
			ctx->code_end = ctx->tmp_code_start;
			ctx->got_end = 1;
			if (ctx->headers_count == 0)
				ctx->seek_end = 1;
			else
				ctx->seek_end = 0;
			break;
		}

		in++;
		(*consumed)++;
	}


	*frame_size = 0;

	if (ctx->got_end == 1) {
		frame_length = ctx->code_end;
	} else
		frame_length = *consumed;


	if (ctx->code_start >= 0) {
		frame_length -= ctx->code_start;
		in = in_orig + ctx->code_start;
	} else {
		memcpy(out, ctx->bytes, -ctx->code_start);
		*frame_size += -ctx->code_start;
		out += -ctx->code_start;
		in_size -= -ctx->code_start;
		in = in_orig;
	}

	if (ctx->got_start) {
		if (out_size < frame_length) {
			err("Output buffer too small for current frame");
			return 0;
		}
		memcpy(out, in, frame_length);
		*frame_size += frame_length;

		if (ctx->got_end) {
			ctx->code_start = ctx->code_end - *consumed;
			ctx->got_start = 1;
			ctx->got_end = 0;
			frame_finished = 1;
			if (ctx->last_tag == H264_TAG_SLICE) {
				ctx->seek_end = 1;
				ctx->main_count = 0;
				ctx->headers_count = 0;
			} else {
				ctx->seek_end = 0;
				ctx->main_count = 0;
				ctx->headers_count = 1;
			}
			memcpy(ctx->bytes, in_orig + ctx->code_end, *consumed - ctx->code_end);
		} else {
			ctx->code_start = 0;
			frame_finished = 0;
		}
	}

	ctx->tmp_code_start -= *consumed;

	return frame_finished;
}

/* Evaluates to a mask with n bits set */
#define BITS_MASK(n) ((1<<(n))-1)

/* Returns len bits, with the LSB at position bit */
#define BITS_GET(val, bit, len) (((val)>>(bit))&BITS_MASK(len))

#define CHECK_FOR_UPDATE(lval,rval,update_flag) do {\
        unsigned int old = lval; \
        update_flag |= (old != (lval = rval)); \
    } while(0)

#define FRAME_HEADER_SZ		3
#define KEYFRAME_HEADER_SZ	7

struct vp8_frame_hdr {
    unsigned int is_keyframe;      /* Frame is a keyframe */
    unsigned int is_experimental;  /* Frame is a keyframe */
    unsigned int version;          /* Bitstream version */
    unsigned int is_shown;         /* Frame is to be displayed. */
    unsigned int part0_sz;         /* Partition 0 length, in bytes */

    struct vp8_kf_hdr
    {
        unsigned int w;        /* Width */
        unsigned int h;        /* Height */
        unsigned int scale_w;  /* Scaling factor, Width */
        unsigned int scale_h;  /* Scaling factor, Height */
    } kf;

    unsigned int frame_size_updated; /* Flag to indicate a resolution
                                      * update.
                                      */
};

#define IVF_FILE_HEADER_SIZE	32
#define IVF_FRAME_HEADER_SIZE	12

#define fourcc(a, b, c, d)		\
	((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/*
An IVF file begins with a 32-byte header:
	bytes 0-3    signature: 'DKIF'
	bytes 4-5    version (should be 0)
	bytes 6-7    length of header in bytes
	bytes 8-11   codec FourCC (e.g., 'VP80')
	bytes 12-13  width in pixels
	bytes 14-15  height in pixels
	bytes 16-19  frame rate
	bytes 20-23  time scale
	bytes 24-27  number of frames in file
	bytes 28-31  unused

Each frame consists of a 12-byte header followed by data:
	bytes 0-3    size of frame in bytes (not including the 12-byte header)
	bytes 4-11   64-bit presentation timestamp
	bytes 12..   frame data

*/
static int parse_vpx_ivf_header(struct parser_context *ctx, const char *in)
{
	uint32_t ivf_magic, codec, frate, ts, num_frames, unused;
	uint16_t version, hdr_len, w, h;
	int off = 0;

	if (ctx->is_32Bhdr_found)
		return 0;

	memcpy(&ivf_magic, in + off, 4);
	off += 4;
	memcpy(&version, in + off, 2);
	off += 2;
	memcpy(&hdr_len, in + off, 2);
	off += 2;
	memcpy(&codec, in + off, 4);
	off += 4;
	memcpy(&w, in + off, 2);
	off += 2;
	memcpy(&h, in + off, 2);
	off += 2;
	memcpy(&frate, in + off, 4);
	off += 4;
	memcpy(&ts, in + off, 4);
	off += 4;
	memcpy(&num_frames, in + off, 4);
	off += 4;
	memcpy(&unused, in + off, 4);
	off += 4;

	if (ivf_magic == fourcc('D', 'K', 'I', 'F')) {
		dbg("it's IVF file!");
	} else {
		return -1;
	}

	ctx->is_32Bhdr_found = 1;

	dbg("version:%u, hdr_len:%u, %ux%u, frate:%u, ts:%u, num_frames:%u, off:%u",
		version, hdr_len, w, h, frate, ts, num_frames, off);

	return off;
}

static int parse_vpx_ivf_framesize(const char *in, uint32_t *fsize)
{
	uint32_t framesize;
	uint64_t pts;
	int off = 0;

	memcpy(&framesize, in + off, 4);
	off += 4;
	memcpy(&pts, in + off, 8);
	off += 8;

	if (fsize)
		*fsize = framesize;

	dbg("framesize:%u, pts:%lu", framesize, pts);

	return off;
}

int parser_vp8(struct parser_context *ctx,
	       char *in, int in_size,
	       char *out, int out_size,
	       int *consumed, int *frame_size, int get_head)
{
	unsigned long raw;
	char *data = in, *frame_start = NULL;
	struct vp8_frame_hdr hdr = {0};
	int frame_finished = 0, ret;
	uint32_t framesz;
	uint32_t pos = 0;
	int is_32B_hdr = 0;

	if (in_size < 10) {
		err("ivalid size %d", in_size);
		return -1;
	}

	*consumed = 0;

	ret = parse_vpx_ivf_header(ctx, data);
	if (ret == -1)
		return -1;

	if (ret > 0)
		is_32B_hdr = 1;

	data += ret;
	pos += ret;

	ret = parse_vpx_ivf_framesize(data, &framesz);
	if (ret == -1)
		return -1;

	data += ret;
	frame_start = data;
	pos += ret;
	pos += framesz;

	raw = data[0] | (data[1] << 8) | (data[2] << 16);

	hdr.is_keyframe     = !BITS_GET(raw, 0, 1);
	hdr.version         = BITS_GET(raw, 1, 2);
	hdr.is_experimental = BITS_GET(raw, 3, 1);
	hdr.is_shown        = BITS_GET(raw, 4, 1);
	hdr.part0_sz        = BITS_GET(raw, 5, 19);

	if (in_size <= hdr.part0_sz + (hdr.is_keyframe ? 10 : 3)) {
		err("%d < %d", in_size,
			hdr.part0_sz + (hdr.is_keyframe ? 10 : 3));
		return -1;
	}

	hdr.frame_size_updated = 0;

	if (hdr.is_keyframe) {
		unsigned int update = 0;

		/* Keyframe header consists of a three byte sync code followed
		 * by the width and height and associated scaling factors.
		 */
		if (data[3] != 0x9d || data[4] != 0x01 || data[5] != 0x2a) {
			err("keyframe header is missing");
			return -1;
		}

		raw = data[6] | (data[7] << 8) | (data[8] << 16) |
		     (data[9] << 24);
		CHECK_FOR_UPDATE(hdr.kf.w,       BITS_GET(raw,  0, 14), update);
		CHECK_FOR_UPDATE(hdr.kf.scale_w, BITS_GET(raw, 14,  2), update);
		CHECK_FOR_UPDATE(hdr.kf.h,       BITS_GET(raw, 16, 14), update);
		CHECK_FOR_UPDATE(hdr.kf.scale_h, BITS_GET(raw, 30,  2), update);

		hdr.frame_size_updated = update;

		if (!hdr.kf.w || !hdr.kf.h) {
			err("keyframe width or height (%ux%u)",
				hdr.kf.w, hdr.kf.h);
			return -1;
		}
	}

#if 0
	data += FRAME_HEADER_SZ;
	in_size -= FRAME_HEADER_SZ;
	*consumed = FRAME_HEADER_SZ;

	if (hdr.is_keyframe) {
		data += KEYFRAME_HEADER_SZ;
		in_size -= KEYFRAME_HEADER_SZ;
		*consumed += KEYFRAME_HEADER_SZ;
	}

	*frame_size = hdr.is_keyframe ? 10 : 3;
	*frame_size += hdr.part0_sz;
	*consumed += hdr.part0_sz;
#endif
	frame_finished = 1;

	memcpy(out, frame_start, framesz);

	dbg("is_keyframe:%u, part0_sz:%u, WxH:%ux%u, consumed:%d, frame_size:%d",
		hdr.is_keyframe, hdr.part0_sz, hdr.kf.w, hdr.kf.h,
		*consumed, *frame_size);

	*frame_size = framesz;
	*consumed = framesz;
	*consumed += 12;
	*consumed += is_32B_hdr ? 32 : 0;

	dbg("framesz: %u, consumed: %u, pos: %u (in:%p)", *frame_size,
		*consumed, pos, in);

	return frame_finished;
}

void parser_destroy(struct parser_context *ctx)
{
	struct input_file *input = &ctx->input;

	if (!ctx)
		return;

	if (input->fd > 0) {
		munmap(input->data, input->size);
		close(input->fd);
	}

	free(ctx);

	free(tmp_buffer);
	tmp_buffer = NULL;

	dbg("parser_destroy");
}

int parser_create(struct parser_context **ctx, const char *url,
		  unsigned int codec,
		  unsigned int len, char *data)
{
	struct parser_context *context;
	struct input_file *input;
	struct stat in_stat;

	*ctx = NULL;

	if (!url && !data)
		return -1;

	context = malloc(sizeof(*context));
	if (!context)
		return -1;

	tmp_buffer = malloc(1024*1024);
	if (!tmp_buffer) {
		free(context);
		return -1;
	}

	memset(context, 0, sizeof(*context));

	switch (codec) {
	case V4L2_PIX_FMT_MPEG1:
	case V4L2_PIX_FMT_MPEG2:
		context->func = parser_mpeg2;
		break;
	case V4L2_PIX_FMT_XVID:
	case V4L2_PIX_FMT_H263:
	case V4L2_PIX_FMT_MPEG4:
		context->func = parser_mpeg4;
		break;
	case V4L2_PIX_FMT_H264:
		context->func = parser_h264;
		break;
	case V4L2_PIX_FMT_VP8:
		context->func = parser_vp8;
		break;
	case V4L2_PIX_FMT_VP9:
		goto error;
	case V4L2_PIX_FMT_HEVC:
		goto error;
	default:
		goto error;
	}

	input = &context->input;
	input->offs = 0;

	if (url) {
		input->fd = open(url, O_RDONLY);
		if (input->fd <= 0) {
			err("failed to open file: %s", url);
			goto error;
		}

		fstat(input->fd, &in_stat);

		input->size = in_stat.st_size;
		input->data = mmap(0, input->size, PROT_READ, MAP_SHARED,
				   input->fd, 0);
		if (input->data == MAP_FAILED) {
			err("failed to map input file");
			goto error;
		}
	} else if (data){
		input->size = len;
		input->data = data;
	}

	*ctx = context;

	dbg("ctx:%p, input: size:%u, data:%p", ctx, input->size, input->data);

	return 0;
error:
	free(context);
	return -1;
}

int parser_read_frame(struct parser_context *ctx, char *data, unsigned int size,
		      unsigned int *framesize, int stream_header)
{
	struct input_file *input = &ctx->input;
	int consumed, fs;
	int ret;

	if (!data)
		data = tmp_buffer;

	ret = ctx->func(ctx,
			input->data + input->offs,
			input->size - input->offs,
			data, size,
			&consumed, &fs, stream_header);

	dbg("consumed: %d, framesize: %d, frame: %d, stream_header: %d, ret: %d",
		consumed, fs, ret, stream_header, ret);

	input->offs += consumed;

	if (framesize)
		*framesize = fs;

	return ret;
}
