/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Main file of the application
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "args.h"
#include "common.h"
#include "fileops.h"
#include "video.h"
#include "parser.h"
#include "tracer.h"
#include "ctrls.h"

#define STREAM_BUFFER_SIZE	(4 * 1024 * 1024)

static int event_recv = 0;

static const unsigned int event_types[] = {
	V4L2_EVENT_EOS,
	V4L2_EVENT_SOURCE_CHANGE,
};

int events_unsubscribe(struct video *vid)
{
	int size_event = sizeof(event_types) / sizeof(event_types[0]);
	int i, ret;

	for (i = 0; i < size_event; i++) {
		ret = video_event_unsubscribe(vid, event_types[i]);
		if (ret < 0) {
			err("cannot unsubscribe event type %d (%s)",
			    event_types[i], strerror(errno));
			return ret;
		}
	}

	return 0;
}

static int events_subscribe(struct video *vid)
{
	int n_events = sizeof(event_types) / sizeof(event_types[0]);
	int i, ret;

	for (i = 0; i < n_events; i++) {
		ret = video_event_subscribe(vid, event_types[i]);
		if (ret < 0) {
			err("cannot subscribe event type %d (%s)",
			    event_types[i], strerror(errno));
			return ret;
		}
	}

	return 0;
}

static int events_handler(struct instance *inst)
{
	struct video *vid = &inst->video;
	struct v4l2_event event;
	unsigned int w, h;
	int ret;

	memset(&event, 0, sizeof(event));

	ret = video_event_dequeue(vid, &event);
	if (ret < 0) {
		err("cannot dequeue events (%s)", strerror(errno));
		return -errno;
	}

	switch (event.type) {
	case V4L2_EVENT_EOS:
		info("EOS reached");
		break;
	case V4L2_EVENT_SOURCE_CHANGE:
		info("Source changed");
		ret = video_gfmt_cap(vid, &w, &h, NULL);
		if (!ret)
			info("new resolution is %ux%u", w, h);

		ctrl_get_min_bufs_for_capture(inst);
		event_recv = 1;
		break;
	default:
		dbg("unknown event type occurred %x", event.type);
		break;
	}

	return 0;
}

static int save_frame(struct instance *i, const void *buf, unsigned int size)
{
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	char filename[64];
	int fd;
	int ret;
	static unsigned int frame_num = 0;

	if (!i->save_frames)
		return 0;

	ret = sprintf(filename, "%s/frame%04d.nv12", i->save_path, frame_num);
	if (ret < 0) {
		err("sprintf fail (%s)", strerror(errno));
		return -1;
	}

	fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, mode);
	if (fd < 0) {
		err("cannot open file (%s)", strerror(errno));
		return -1;
	}

	ret = write(fd, buf, size);
	if (ret < 0) {
		err("cannot write to file (%s)", strerror(errno));
		return -1;
	}

	close(fd);

	frame_num++;

	return 0;
}

static void *parser_thread_func(void *args)
{
	struct instance *inst = args;
	struct video *vid = &inst->video;
	struct parser_context *parser = inst->parser_ctx;
	unsigned int idx, fs;
	int ret, last_parser_pkt = 0;

	dbg("Parser thread started");

	while (!inst->error && !inst->finish) {
		idx = 0;
		pthread_mutex_lock(&inst->lock);
		while (idx < vid->out_buf_cnt && vid->out_buf_flag[idx])
			idx++;
		pthread_mutex_unlock(&inst->lock);

		if (idx < vid->out_buf_cnt) {

			if (last_parser_pkt) {
				inst->finish = 1;
//				fs = 0;
//				ret = video_qbuf_out(vid, idx, fs);
//				pthread_mutex_lock(&inst->lock);
//				vid->out_buf_flag[idx] = 1;
//				pthread_mutex_unlock(&inst->lock);
				continue;
			}

			ret = parser_read_frame(parser,
						vid->out_buf_addr[idx],
						vid->out_buf_size,
						&fs,
						0);

			if (!ret || (parser->input.offs == parser->input.size)) {
				dbg("Last parser frame (fs: %u)", fs);
				last_parser_pkt = 1;
			}

			dbg("Extracted frame of size %d", fs);

			if (fs > vid->out_buf_size)
				err("consumed %u, out buf sz %u",
				    fs, vid->out_buf_size);

			ret = video_qbuf_out(vid, idx, fs);

			pthread_mutex_lock(&inst->lock);
			vid->out_buf_flag[idx] = 1;
			pthread_mutex_unlock(&inst->lock);
		} else {
			pthread_mutex_lock(&inst->lock);
			pthread_cond_wait(&inst->cond, &inst->lock);
			pthread_mutex_unlock(&inst->lock);
		}
	}

	dbg("Parser thread finished");

	return NULL;
}

static int handle_capture(struct instance *inst)
{
	struct video *vid = &inst->video;
	unsigned int bytesused, idx, finished, seq = 0;
	int ret;

	dbg("dequeuing capture buffer");

	tracer_buf_start(inst, TYPE_DQBUF);

	ret = video_dqbuf_cap(vid, &idx, &finished, &bytesused, &seq);

	tracer_buf_finish(inst, TYPE_DQBUF);

	if (ret < 0)
		goto done;

	vid->cap_buf_flag[idx] = 0;

	fprintf(stdout, "%08ld\b\b\b\b\b\b\b\b", vid->total_captured);
	fflush(stdout);

	if (finished)
		goto done;

	vid->total_captured++;

	save_frame(inst, vid->cap_buf_addr[idx][0], bytesused);

	tracer_buf_start(inst, TYPE_QBUF);

	ret = video_qbuf_cap(vid, idx);

	tracer_buf_finish(inst, TYPE_QBUF);

	if (ret < 0)
		err("qbuf capture error %d", ret);

done:
	if (!ret)
		vid->cap_buf_flag[idx] = 1;

	dbg("seq: %u, captured: %lu\n", seq, vid->total_captured);

	return ret;
}

static int handle_output(struct instance *inst)
{
	struct video *vid = &inst->video;
	unsigned int idx;
	int ret;

	ret = video_dqbuf_out(vid, &idx);
	if (ret < 0) {
		err("dequeue output buffer fail");
	} else {
		pthread_mutex_lock(&inst->lock);
		vid->out_buf_flag[idx] = 0;
		pthread_mutex_unlock(&inst->lock);
		pthread_cond_signal(&inst->cond);
	}

	return ret;
}

static void *main_thread_func(void *args)
{
	struct instance *inst = args;
	struct video *vid = &inst->video;
	struct pollfd pfd;
	short revents;
	int ret;

	dbg("main thread started");

	pfd.fd = vid->fd;
	pfd.events = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM |
		     POLLRDBAND | POLLPRI;

	fprintf(stdout, "decoded frame ");

	while (1) {
		if (inst->finish)
			break;

		ret = poll(&pfd, 1, 2000);
		if (!ret) {
			inst->error = 1;
			err("poll timeout");
			break;
		} else if (ret < 0) {
			inst->error = 1;
			err("poll error");
			break;
		}

		revents = pfd.revents;

		if (revents & POLLPRI)
			events_handler(inst);

		if (revents & (POLLIN | POLLRDNORM)) {
			ret = handle_capture(inst);
			if (ret)
				break;
		}

		if (revents & (POLLOUT | POLLWRNORM)) {
			ret = handle_output(inst);
			if (ret)
				break;
		}
	}

	if (inst->error)
		pthread_cond_signal(&inst->cond);

	dbg("main thread finished");

	return NULL;
}

static void cleanup(struct instance *inst)
{
	video_close(&inst->video);
	parser_destroy(inst->parser_ctx);
}

static int drain_loop(struct instance *inst)
{
	int ret;

	ret = video_dec_stop(&inst->video);
	if (ret)
		err("decoder stop command fail %d", ret);

	while (1) {
		ret = handle_capture(inst);
		if (ret == -EPIPE || ret == -EIO)
			break;
	}

	/* Drain loop should exits with EPIPE */
	if (ret != -EPIPE)
		err("Drain loop exits with error %d", ret);

	return ret;
}

int main(int argc, char **argv)
{
	struct instance inst;
	struct video *vid = &inst.video;
	pthread_t parser_thread;
	pthread_t main_thread;
	int ret, n;

	memset(&inst, 0, sizeof(inst));

	ret = parse_args(&inst, argc, argv);
	if (ret) {
		print_usage(argv[0]);
		return -1;
	}

	info("decoding resolution %dx%d", inst.width, inst.height);

	pthread_mutex_init(&inst.lock, 0);
	pthread_condattr_init(&inst.attr);
	pthread_cond_init(&inst.cond, &inst.attr);

	ret = tracer_init(&inst);
	if (ret)
		goto err;

	ret = parser_create(&inst.parser_ctx, inst.file_name, inst.codec, 0, NULL);
	if (ret)
		goto err;

	ret = video_open(vid, inst.video.name);
	if (ret)
		goto err;

	ret = events_subscribe(vid);
	if (ret)
		goto err;

	ret = video_set_framerate(vid, 30);
	if (ret)
		goto err;

	ret = video_sfmt_out(vid, inst.width, inst.height, inst.codec);
	if (ret)
		goto err;

	ret = video_setup_output(vid, STREAM_BUFFER_SIZE, 4);
	if (ret)
		goto err;

	ret = video_set_control(vid);
	if (ret)
		goto err;

	ret = video_streamon_out(vid);
	if (ret)
		goto err;

	if (pthread_create(&parser_thread, NULL, parser_thread_func, &inst))
		goto err;

	if (pthread_create(&main_thread, NULL, main_thread_func, &inst))
		goto err;

	while (!event_recv)
		usleep(1000);
	event_recv = 0;

	info("event received");

	ret = video_setup_capture(vid, 4, inst.width, inst.height);
	if (ret)
		goto err;

	ret = video_streamon_cap(vid);
	if (ret)
		goto err;

	for (n = 0; n < vid->cap_buf_cnt; n++) {
		if (inst.video.cap_use_dmabuf)
			ret = video_qbuf_cap_dmabuf(vid, n, 0);
		else
			ret = video_qbuf_cap(vid, n);

		if (ret)
			goto err;

		vid->cap_buf_flag[n] = 1;
	}

	pthread_join(parser_thread, 0);
	pthread_join(main_thread, 0);

	dbg("Threads have finished");

	if (!inst.error && inst.finish)
		ret = drain_loop(&inst);

	video_stop(vid);
	events_unsubscribe(vid);
	tracer_show(&inst);
	tracer_deinit(&inst);
	pthread_mutex_destroy(&inst.lock);
	pthread_cond_destroy(&inst.cond);
	pthread_condattr_destroy(&inst.attr);

	cleanup(&inst);

	info("Total frames captured %ld", vid->total_captured);

	return 0;
err:
	cleanup(&inst);
	return 1;
}
