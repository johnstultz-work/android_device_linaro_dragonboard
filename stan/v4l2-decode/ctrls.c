#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#include "ctrls.h"

#define DBG_TAG "   ctrls"

int ctrl_get_min_bufs_for_capture(struct instance *i)
{
	struct v4l2_control ctrl = {0};
	int ret;

	ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;

	ret = ioctl(i->video.fd, VIDIOC_G_CTRL, &ctrl);
	if (ret) {
		err("get min capture buffers fail (%s)", strerror(errno));
		return ret;
	}

	i->video.cap_min_bufs = ctrl.value;

	info("minimum capture buffers count: %u", i->video.cap_min_bufs);

	return 0;
}

