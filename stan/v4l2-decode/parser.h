/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Really simple stream parser header file
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

#ifndef _PARSER_H_
#define _PARSER_H_

/* H264 parser states */
enum h264_parser_state {
	H264_PARSER_NO_CODE,
	H264_PARSER_CODE_0x1,
	H264_PARSER_CODE_0x2,
	H264_PARSER_CODE_0x3,
	H264_PARSER_CODE_1x1,
	H264_PARSER_CODE_SLICE,
};

/* H264 recent tag type */
enum h264_tag_type {
	H264_TAG_HEAD,
	H264_TAG_SLICE,
};

/* MPEG4 parser states */
enum mpeg4_parser_state {
	MPEG4_PARSER_NO_CODE,
	MPEG4_PARSER_CODE_0x1,
	MPEG4_PARSER_CODE_0x2,
	MPEG4_PARSER_CODE_1x1,
};

/* MPEG4 recent tag type */
enum mpeg4_tag_type {
	MPEG4_TAG_HEAD,
	MPEG4_TAG_VOP,
};

struct input_file {
	int fd;
	char *data;
	int size;
	int offs;
};

struct parser_context;

/* Parse the stream:
 * - consumed is used to return the number of bytes consumed from the output
 * - frame_size is used to return the size of the frame that has been extracted
 * - get_head - when equal to 1 it is used to extract the stream header when
 *   setting up v4l2 decoder
 * Return value: 1 - if a complete frame has been extracted, 0 otherwise
 */

typedef int (*parser_func)(struct parser_context *ctx,
			   char* in, int in_size,
			   char* out, int out_size,
			   int *consumed, int *frame_size, int get_head);

/* Parser context */
struct parser_context {
	int state;
	int last_tag;
	char bytes[6];
	int main_count;
	int headers_count;
	int tmp_code_start;
	int code_start;
	int code_end;
	char got_start;
	char got_end;
	char seek_end;
	int short_header;

	/* VP8 */
	int is_32Bhdr_found;
	parser_func func;
	struct input_file input;
};

int parser_create(struct parser_context **ctx, const char *url,
		  unsigned int codec,
		  unsigned int len, char *data);

void parser_destroy(struct parser_context *ctx);

int parser_read_frame(struct parser_context *ctx, char *data, unsigned int size,
		      unsigned int *framesize, int stream_header);

#endif /* _PARSER_H_ */
