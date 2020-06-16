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

#include <linux/videodev2.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "video.h"
#include "common.h"

#ifndef V4L2_BUF_FLAG_LAST
#define V4L2_BUF_FLAG_LAST		0x00100000
#endif

static char *dbg_type[2] = {"OUTPUT", "CAPTURE"};
static char *dbg_status[2] = {"ON", "OFF"};

/* check is it a decoder video device */
static int is_video_decoder(int fd, const char *name)
{
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fdesc;
	int found = 0;

	memzero(cap);
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		err("Failed to verify capabilities: %m");
		return -1;
	}

	dbg("caps (%s): driver=\"%s\" bus_info=\"%s\" card=\"%s\" "
	    "version=%u.%u.%u", name, cap.driver, cap.bus_info, cap.card,
	    (cap.version >> 16) & 0xff,
	    (cap.version >> 8) & 0xff,
	     cap.version & 0xff);

	if (!(cap.capabilities & V4L2_CAP_STREAMING) ||
	    !(cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
		err("Insufficient capabilities for video device (is %s correct?)",
		    name);
		return -1;
	}

	memzero(fdesc);
	fdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	while (!ioctl(fd, VIDIOC_ENUM_FMT, &fdesc)) {
		dbg("  %s", fdesc.description);

		switch (fdesc.pixelformat) {
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV12_UBWC:
			found = 1;
			break;
		default:
			dbg("%s is not a decoder video device", name);
			return -1;
		}

		if (found)
			break;

		fdesc.index++;
	}

	found = 0;
	memzero(fdesc);
	fdesc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	while (!ioctl(fd, VIDIOC_ENUM_FMT, &fdesc)) {
		dbg("  %s", fdesc.description);

		switch (fdesc.pixelformat) {
		case V4L2_PIX_FMT_MPEG:
		case V4L2_PIX_FMT_H264:
		case V4L2_PIX_FMT_H263:
		case V4L2_PIX_FMT_MPEG1:
		case V4L2_PIX_FMT_MPEG2:
		case V4L2_PIX_FMT_MPEG4:
		case V4L2_PIX_FMT_XVID:
		case V4L2_PIX_FMT_VC1_ANNEX_G:
		case V4L2_PIX_FMT_VC1_ANNEX_L:
		case V4L2_PIX_FMT_VP8:
			found = 1;
			break;
		default:
			err("%s is not a decoder video device", name);
			return -1;
		}

		if (found)
			break;

		fdesc.index++;
	}

	return 0;
}

int video_open(struct video *vid, const char *name)
{
	char video_name[64];
	unsigned idx = 0, v = 0;
	int ret, fd = -1;

	while (v++ < 10) {
		memset(video_name, 0, sizeof(video_name));
		ret = sprintf(video_name, "/dev/video%d", idx);
		if (ret < 0)
			return ret;

		idx++;

		dbg("open video device: %s", video_name);

		fd = open(video_name, O_RDWR | O_NONBLOCK, 0);
		if (fd < 0) {
			err("Failed to open video device: %s (%s)", video_name,
				strerror(errno));
			continue;
		}

		ret = is_video_decoder(fd, video_name);
		if (ret < 0) {
			close(fd);
			continue;
		}

		if (!ret)
			break;
	}

	if (ret < 0)
		fd = open(name, O_RDWR | O_NONBLOCK, 0);

	if (fd < 0) {
		err("Failed to open video device: %s (%s)", name, strerror(errno));
		return -1;
	}

	vid->fd = fd;

        return 0;
}


void video_close(struct video *vid)
{
	if (vid->fd > 0)
		close(vid->fd);
}

int video_set_control(struct video *vid)
{
	return 0;
}

int video_set_framerate(struct video *vid, unsigned int framerate)
{
	struct v4l2_streamparm parm = {0};

	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	parm.parm.output.timeperframe.numerator = 1;
	parm.parm.output.timeperframe.denominator = framerate;

	return ioctl(vid->fd, VIDIOC_S_PARM, &parm);
}

int video_buf_exp(struct video *vid, unsigned int index)
{
	struct v4l2_exportbuffer expbuf;
	unsigned int num_planes = CAP_PLANES;
	unsigned int n;

	for (n = 0; n < num_planes; n++) {
		memset(&expbuf, 0, sizeof(expbuf));

		expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		expbuf.index = index;
		expbuf.flags = O_CLOEXEC | O_RDWR;
		expbuf.plane = n;

		if (ioctl(vid->fd, VIDIOC_EXPBUF, &expbuf) < 0) {
			err("CAPTURE: Failed to export buffer index%u (%s)",
			    index, strerror(errno));
			return -1;
		}

		info("CAPTURE: Exported buffer index%u (plane%u) with fd %d",
		     index, n, expbuf.fd);
	}

	return 0;
}

int video_buf_create(struct video *vid, unsigned int width,
		     unsigned int height,unsigned int *index,
		     unsigned int count)
{
	struct v4l2_create_buffers b;
	int ret;

	memzero(b);
	b.count = count;
	b.memory = V4L2_MEMORY_MMAP;
	b.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	b.format.fmt.pix_mp.width = width;
	b.format.fmt.pix_mp.height = height;
	b.format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;

	ret = ioctl(vid->fd, VIDIOC_CREATE_BUFS, &b);
	if (ret) {
		err("Failed to create bufs index%u (%s)", b.index,
		    strerror(errno));
		return -1;
	}

	*index = b.index;

	info("create_bufs: index %u, count %u", b.index, b.count);
	return 0;
}

static int video_qbuf(struct video *vid, unsigned int index,unsigned int length,
		      unsigned int type, unsigned int nplanes)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[2];
	int ret;

	memzero(buf);
	memset(planes, 0, sizeof(planes));

	buf.type = type;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = index;
	buf.length = nplanes;
	buf.m.planes = planes;
	buf.m.planes[0].bytesused = length;
	buf.m.planes[0].data_offset = 0;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		buf.m.planes[0].length = vid->cap_buf_size[0];
	} else {
		buf.m.planes[0].length = vid->out_buf_size;
		if (length == 0)
			buf.flags |= V4L2_BUF_FLAG_LAST;
	}

	ret = ioctl(vid->fd, VIDIOC_QBUF, &buf);
	if (ret) {
		err("QBUF: Failed to queue buffer (index=%u) on %s (%s)",
		    buf.index,
		    dbg_type[type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE],
		    strerror(errno));
		return -errno;
	}

	dbg("QBUF: buffer on %s queue with index %u",
	    dbg_type[type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE], buf.index);

	return 0;
}

int video_qbuf_out(struct video *vid, unsigned int idx, unsigned int length)
{
	if (idx >= vid->out_buf_cnt) {
		err("Tried to queue a non exisiting buffer");
		return -1;
	}

	return video_qbuf(vid, idx, length, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
			  OUT_PLANES);
}

int video_qbuf_cap(struct video *vid, unsigned int idx)
{
	if (idx >= vid->cap_buf_cnt) {
		err("Tried to queue a non exisiting buffer");
		return -1;
	}

	return video_qbuf(vid, idx, vid->cap_buf_size[0],
			  V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
			  CAP_PLANES);
}

int video_qbuf_cap_dmabuf(struct video *vid, unsigned int idx, int dbuf_fd)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[2];
	int ret;

	if (idx >= vid->cap_buf_cnt) {
		err("Tried to queue a non exisiting buffer");
		return -1;
	}

	memzero(buf);
	memset(planes, 0, sizeof(planes));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_DMABUF;
	buf.index = idx;
	buf.length = CAP_PLANES;
	buf.m.planes = planes;

	if (dbuf_fd > 0)
		buf.m.planes[0].m.fd = dbuf_fd;

	buf.m.planes[0].bytesused = vid->cap_buf_size[0];
	buf.m.planes[1].bytesused = vid->cap_buf_size[1];

	buf.m.planes[0].data_offset = 0;
	buf.m.planes[1].data_offset = 0;

	buf.m.planes[0].length = vid->cap_buf_size[0];

	ret = ioctl(vid->fd, VIDIOC_QBUF, &buf);
	if (ret) {
		err("QBUF: Failed to queue buffer (index=%u) on CAPTURE (%s)",
		    buf.index, strerror(errno));
		return -1;
	}

	dbg("  QBUF: Queued buffer on %s queue with index %u",
	    dbg_type[1], buf.index);

	return 0;
}

static int video_dqbuf(struct video *vid, struct v4l2_buffer *buf)
{
	int ret;

	ret = ioctl(vid->fd, VIDIOC_DQBUF, buf);
	if (ret < 0)
		return -errno;

	dbg("Dequeued buffer on %s queue with index %u (flags:%x, bytesused:%u)",
	    dbg_type[buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE],
	    buf->index, buf->flags, buf->m.planes[0].bytesused);

	return 0;
}

int video_dqbuf_out(struct video *vid, unsigned int *idx)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[OUT_PLANES];
	int ret;

	memzero(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.m.planes = planes;
	buf.length = OUT_PLANES;

	ret = video_dqbuf(vid, &buf);
	if (ret < 0)
		return ret;

	if (idx)
		*idx = buf.index;

	return 0;
}

int video_dqbuf_cap(struct video *vid, unsigned int *idx, unsigned int *finished,
		    unsigned int *bytesused, unsigned int *sequence)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[CAP_PLANES];
	int ret;

	memset(planes, 0, sizeof(planes));
	memzero(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (vid->cap_use_dmabuf)
		buf.memory = V4L2_MEMORY_DMABUF;
	else
		buf.memory = V4L2_MEMORY_MMAP;
	buf.m.planes = planes;
	buf.length = CAP_PLANES;

	ret = video_dqbuf(vid, &buf);
	if (ret < 0)
		return ret;

	if (finished)
		*finished = 0;

	if (buf.flags & V4L2_BUF_FLAG_LAST || buf.m.planes[0].bytesused == 0) {
		if (finished)
			*finished = 1;
	}

	if (sequence)
		*sequence = buf.sequence;

	if (bytesused)
		*bytesused = buf.m.planes[0].bytesused;
	if (idx)
		*idx = buf.index;

	return 0;
}

static int video_stream(struct video *vid, enum v4l2_buf_type type, unsigned int status)
{
	int ret;

	ret = ioctl(vid->fd, status, &type);
	if (ret < 0) {
		err("Failed to change streaming (type=%s, status=%s)",
		    dbg_type[type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE],
		    dbg_status[status == VIDIOC_STREAMOFF]);
		return -errno;
	}

	info("Stream %s on %s queue", dbg_status[status==VIDIOC_STREAMOFF],
	     dbg_type[type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE]);

	return 0;
}

int video_streamon_cap(struct video *vid)
{
	return video_stream(vid, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
			    VIDIOC_STREAMON);
}

int video_streamon_out(struct video *vid)
{
	return video_stream(vid, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
			    VIDIOC_STREAMON);
}

int video_streamoff_cap(struct video *vid)
{
	return video_stream(vid, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
			    VIDIOC_STREAMOFF);
}

int video_streamoff_out(struct video *vid)
{
	return video_stream(vid, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
			    VIDIOC_STREAMOFF);
}

int video_dec_stop(struct video *vid)
{
	struct v4l2_decoder_cmd dec;
	int ret;

	memzero(dec);
	dec.cmd = V4L2_DEC_CMD_STOP;

	ret = ioctl(vid->fd, VIDIOC_DECODER_CMD, &dec);
	if (ret < 0) {
		err("DECODER_CMD failed (%s)", strerror(errno));
		return -errno;
	}

	return 0;
}

int video_dec_start(struct video *vid)
{
	struct v4l2_decoder_cmd dec;
	int ret;

	memzero(dec);
	dec.cmd = V4L2_DEC_CMD_START;

	ret = ioctl(vid->fd, VIDIOC_DECODER_CMD, &dec);
	if (ret < 0) {
		err("DECODER_CMD failed (%s)", strerror(errno));
		return -errno;
	}

	return 0;
}

int video_stop(struct video *vid)
{
	struct v4l2_requestbuffers reqbuf;
	unsigned int n;
	int ret;

	ret = video_streamoff_cap(vid);
	if (ret < 0)
		err("STREAMOFF CAPTURE queue failed (%s)", strerror(errno));

	ret = video_streamoff_out(vid);
	if (ret < 0)
		err("STREAMOFF OUTPUT queue failed (%s)", strerror(errno));

	if (!vid->cap_use_dmabuf) {
		for (n = 0; n < vid->cap_buf_cnt; n++) {
			ret = munmap(vid->cap_buf_addr[n][0],
				     vid->cap_buf_size[0]);
			if (ret)
				err("unmap failed %s", strerror(errno));
		}
	}

	for (n = 0; n < vid->out_buf_cnt; n++) {
		ret = munmap(vid->out_buf_addr[n], vid->out_buf_size);
		if (ret)
			err("unmap failed %s", strerror(errno));
	}

	memzero(reqbuf);
	reqbuf.count = 0;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (vid->cap_use_dmabuf)
		reqbuf.memory = V4L2_MEMORY_DMABUF;
	else
		reqbuf.memory = V4L2_MEMORY_MMAP;

	info("calling reqbuf(0)");

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret < 0)
		err("REQBUFS(0) on CAPTURE queue (%s)", strerror(errno));

	reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	reqbuf.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret < 0)
		err("REQBUFS(0) on OUTPUT queue (%s)", strerror(errno));

	return 0;
}

int video_gfmt_cap(struct video *vid, unsigned int *width, unsigned int *height,
		   unsigned int *sizeimage)
{
	struct v4l2_format fmt;
	int ret;

	memzero(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;

	ret = ioctl(vid->fd, VIDIOC_G_FMT, &fmt);
	if (ret) {
		err("CAPTURE: Failed to get format (%s)", strerror(errno));
		return -1;
	}

	*width = fmt.fmt.pix_mp.width;
	*height = fmt.fmt.pix_mp.height;
	if (sizeimage)
		*sizeimage = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	return 0;
}

int video_sfmt_cap(struct video *vid, unsigned int width, unsigned int height,
		   unsigned int pixfmt, unsigned int *sizeimage)
{
	struct v4l2_format fmt;
	int ret;

	memzero(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.width = width;
	fmt.fmt.pix_mp.height = height;
	fmt.fmt.pix_mp.pixelformat = pixfmt;

	ret = ioctl(vid->fd, VIDIOC_S_FMT, &fmt);
	if (ret) {
		err("CAPTURE: Failed to set format (%s)", strerror(errno));
		return -1;
	}

	*sizeimage = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	return 0;
}

int video_sfmt_out(struct video *vid, unsigned int width, unsigned int height,
		   unsigned int pixfmt)
{
	struct v4l2_format fmt;
	int ret;

	memzero(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width = width;
	fmt.fmt.pix_mp.height = height;
	fmt.fmt.pix_mp.pixelformat = pixfmt;

	ret = ioctl(vid->fd, VIDIOC_S_FMT, &fmt);
	if (ret) {
		err("OUTPUT: Failed to set format (%s)", strerror(errno));
		return -1;
	}

	return 0;
}

static int video_setup_capture_mmap(struct video *vid, unsigned int count,
				    unsigned int w, unsigned int h)
{
	struct v4l2_plane planes[CAP_PLANES];
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_format fmt;
	struct v4l2_buffer buf;
	int ret, n;

	memzero(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.height = h;
	fmt.fmt.pix_mp.width = w;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	ret = ioctl(vid->fd, VIDIOC_S_FMT, &fmt);
	if (ret) {
		err("CAPTURE: Failed to set format (%s)", strerror(errno));
		return -1;
	}

	vid->cap_w = fmt.fmt.pix_mp.width;
	vid->cap_h = fmt.fmt.pix_mp.height;
	vid->cap_buf_size[0] = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

	info("CAPTURE: Set format %ux%d sizeimage %u, bpl %u",
	     fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
	     fmt.fmt.pix_mp.plane_fmt[0].sizeimage,
	     fmt.fmt.pix_mp.plane_fmt[0].bytesperline);

#if 1
	memzero(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;

	ret = ioctl(vid->fd, VIDIOC_G_FMT, &fmt);
	if (ret) {
		err("CAPTURE: Failed to get format (%s)", strerror(errno));
		return -1;
	}

	info("CAPTURE: Get format %ux%u sizeimage %u, bpl %u",
	     fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
	     fmt.fmt.pix_mp.plane_fmt[0].sizeimage,
	     fmt.fmt.pix_mp.plane_fmt[0].bytesperline);

	vid->cap_w = fmt.fmt.pix_mp.width;
	vid->cap_h = fmt.fmt.pix_mp.height;
	vid->cap_buf_size[0] = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
#endif

	memzero(reqbuf);
	reqbuf.count = count;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	reqbuf.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret != 0) {
		err("CAPTURE: REQBUFS failed (%s)", strerror(errno));
		return -1;
	}

	info("CAPTURE: Number of buffers is %u (requested %u)",
		reqbuf.count, count);

	vid->cap_buf_cnt = reqbuf.count;

	for (n = 0; n < vid->cap_buf_cnt; n++) {
		memzero(buf);
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n;
		buf.m.planes = planes;
		buf.length = CAP_PLANES;

		ret = ioctl(vid->fd, VIDIOC_QUERYBUF, &buf);
		if (ret != 0) {
			err("CAPTURE: QUERYBUF failed (%s)", strerror(errno));
			return -1;
		}

		vid->cap_buf_off[n][0] = buf.m.planes[0].m.mem_offset;

		vid->cap_buf_addr[n][0] = mmap(NULL, buf.m.planes[0].length,
					       PROT_READ | PROT_WRITE,
					       MAP_SHARED,
					       vid->fd,
					       buf.m.planes[0].m.mem_offset);

		if (vid->cap_buf_addr[n][0] == MAP_FAILED) {
			err("CAPTURE: Failed to MMAP buffer");
			return -1;
		}

		vid->cap_buf_size[0] = buf.m.planes[0].length;
	}

	info("CAPTURE: querybuf: sizeimage %u", vid->cap_buf_size[0]);

	return 0;
}

static int video_setup_capture_dmabuf(struct video *vid, unsigned int count,
				      unsigned int w, unsigned int h)
{
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_format fmt;
	int ret;

	memzero(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.height = h;
	fmt.fmt.pix_mp.width = w;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	ret = ioctl(vid->fd, VIDIOC_S_FMT, &fmt);
	if (ret) {
		err("CAPTURE: Failed to set format (%s)", strerror(errno));
		return -1;
	}

	vid->cap_w = fmt.fmt.pix_mp.width;
	vid->cap_h = fmt.fmt.pix_mp.height;

	vid->cap_buf_size[0] = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
	vid->cap_buf_size[1] = fmt.fmt.pix_mp.plane_fmt[1].sizeimage;

	info("CAPTURE: Set format %ux%d sizeimage %u, bpl %u",
	     fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
	     fmt.fmt.pix_mp.plane_fmt[0].sizeimage,
	     fmt.fmt.pix_mp.plane_fmt[0].bytesperline);

	memzero(reqbuf);
	reqbuf.count = count;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	reqbuf.memory = V4L2_MEMORY_DMABUF;

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret != 0) {
		err("CAPTURE: REQBUFS failed (%s)", strerror(errno));
		return -1;
	}

	info("CAPTURE: Number of buffers is %u (requested %u)",
		reqbuf.count, vid->cap_buf_cnt);

	vid->cap_buf_cnt = reqbuf.count;

	return 0;
}

int video_setup_capture(struct video *vid, unsigned int count,
			unsigned int w, unsigned int h)
{
	int ret;

	if (vid->cap_use_dmabuf)
		ret = video_setup_capture_dmabuf(vid, count, w, h);
	else
		ret = video_setup_capture_mmap(vid, count, w, h);

	return ret;
}

int video_setup_output(struct video *vid, unsigned int size, unsigned int count)
{
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[OUT_PLANES];
	int ret;
	int n;

	memzero(reqbuf);
	reqbuf.count = count;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	reqbuf.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret) {
		err("OUTPUT: REQBUFS failed (%s)", strerror(errno));
		return -1;
	}

	vid->out_buf_cnt = reqbuf.count;

	info("OUTPUT: Number of buffers is %u (requested %u)",
	     vid->out_buf_cnt, count);

	for (n = 0; n < vid->out_buf_cnt; n++) {
		memzero(buf);
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n;
		buf.m.planes = planes;
		buf.length = OUT_PLANES;

		ret = ioctl(vid->fd, VIDIOC_QUERYBUF, &buf);
		if (ret != 0) {
			err("OUTPUT: QUERYBUF failed (%s)", strerror(errno));
			return -1;
		}

		vid->out_buf_off[n] = buf.m.planes[0].m.mem_offset;
		vid->out_buf_size = buf.m.planes[0].length;

		vid->out_buf_addr[n] = mmap(NULL, buf.m.planes[0].length,
					    PROT_READ | PROT_WRITE, MAP_SHARED,
					    vid->fd,
					    buf.m.planes[0].m.mem_offset);

		if (vid->out_buf_addr[n] == MAP_FAILED) {
			err("OUTPUT: Failed to MMAP buffer");
			return -1;
		}

		vid->out_buf_flag[n] = 0;
	}

	info("OUTPUT: querybuf sizeimage %u", vid->out_buf_size);

	return 0;
}

int video_event_subscribe(struct video *vid, unsigned int event_type)
{
	struct v4l2_event_subscription sub;

	memset(&sub, 0, sizeof(sub));
	sub.type = event_type;

	if (ioctl(vid->fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
		err("failed to subscribe to event type %u: %m", sub.type);
		return -1;
	}

	return 0;
}

int video_event_unsubscribe(struct video *vid, unsigned int event_type)
{
	struct v4l2_event_subscription sub;

	memset(&sub, 0, sizeof(sub));
	sub.type = event_type;

	if (ioctl(vid->fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub) < 0) {
		err("failed to unsubscribe to event type %u: %m", sub.type);
		return -1;
	}

	return 0;
}

int video_event_dequeue(struct video *vid, struct v4l2_event *ev)
{
	memset(ev, 0, sizeof (*ev));

	if (ioctl(vid->fd, VIDIOC_DQEVENT, ev) < 0) {
		dbg("failed to dequeue event: %m");
		return -1;
	}

	return 0;
}
