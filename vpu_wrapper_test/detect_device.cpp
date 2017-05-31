/*
 * Copyright 2015 Freescale Semiconductor, Inc. All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/videodev2.h>
#include <errno.h>
#include "common.h"
#include "detect_device.h"
#include "log.h"

int detect_device::enumerate_format (int fd, int type)
{
  struct v4l2_fmtdesc format = {0};
  bool is_encoder = false, is_decoder = false;

  /* format enumeration */
  for (int i = 0; ; i++) {
    format.index = i;
    format.type = type;
    if (ioctl (fd, VIDIOC_ENUM_FMT, &format) < 0) {
      if (errno == EINVAL) {
        break;                  /* end of enumeration */
      } else {
        LOG_ERROR ("VIDIOC_ENUM_FMT fail: %d\n", errno);
        return -1;
      }
    }

    LOG_INFO ("pixelformat: %c%c%c%c\n", format.pixelformat & 0xff,
          (format.pixelformat >> 8) & 0xff, (format.pixelformat >> 16) 
          & 0xff,(format.pixelformat >> 24) & 0xff);
    if ((V4L2_TYPE_IS_OUTPUT(format.type)
        && (format.flags & V4L2_FMT_FLAG_COMPRESSED))
        || (!V4L2_TYPE_IS_OUTPUT(format.type)
        && !(format.flags & V4L2_FMT_FLAG_COMPRESSED))) {
      is_decoder = true;
    } else if ((V4L2_TYPE_IS_OUTPUT(format.type)
        && !(format.flags & V4L2_FMT_FLAG_COMPRESSED))
        || (!V4L2_TYPE_IS_OUTPUT(format.type)
        && (format.flags & V4L2_FMT_FLAG_COMPRESSED))) {
      is_encoder = true;
    }
  }

  if (is_encoder)
    return CODEC_TYPE_ENCODER;
  else if (is_decoder)
    return CODEC_TYPE_DECODER;
  else
    return -1;
}

char *detect_device::find_device (unsigned int type, unsigned int codec_type)
{
  struct v4l2_capability cap = {0};
  struct stat st;
  int codec1, codec2; 
  int fd;
  int i;

  for (i = 0; i < MAX_V4L2_DEVICE; i++) {

    sprintf(devname, "/dev/video%d", i);

    /* check if it is a device     if (stat (devname, &st) == -1) {
      LOG_ERROR ("stat fail\n");
      return NULL;
    }

    if (!S_ISCHR (st.st_mode))
      continue;*/


    fd = open_device (devname);
    if (fd <= 0)
      continue;

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
      LOG_ERROR ("VIDIOC_QUERYCAP error.");
      close_device (fd);
      continue;
    }

    if (!(cap.capabilities & type)) {
      LOG_DEBUG ("device can't capture.");
      close_device (fd);
      continue;
    }

    if (cap.capabilities & V4L2_CAP_VIDEO_M2M) {
      LOG_INFO ("is M2M device.\n");
      codec1 = enumerate_format (fd, V4L2_BUF_TYPE_VIDEO_OUTPUT);
      codec2 = enumerate_format (fd, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    } else {
      LOG_INFO ("is M2M MPLANE device.\n");
      codec1 = enumerate_format (fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
      codec2 = enumerate_format (fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    }
    if (codec1 == CODEC_TYPE_ENCODER && codec2 == CODEC_TYPE_ENCODER) {
      LOG_INFO ("Found video encoder, device name: %s\n", devname);
      if (codec_type == CODEC_TYPE_ENCODER) {
        return devname;
      }
    } else if (codec1 == CODEC_TYPE_DECODER && codec2 == CODEC_TYPE_DECODER) {
      LOG_INFO ("Found video decoder, device name: %s\n", devname);
      if (codec_type == CODEC_TYPE_DECODER) {
        return devname;
      }
    }

    close_device (fd);
  }
}

void detect_device::list_codec ()
{
  find_device (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE,
      -1);
  return;
}

char *detect_device::get_device (unsigned int codec_type)
{
  return find_device (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE,
      codec_type);
}

int detect_device::open_device (char *device)
{
  int fd;

  LOG_INFO ("device name: %s\n", device);
  fd = open(device, O_RDWR | O_NONBLOCK, 0);
  if (fd <= 0) {
    LOG_DEBUG ("Can't open %s.\n", device);
    return -1;
  }
  return fd;
}

void detect_device::close_device (int fd)
{
  if (fd)
    close (fd);
}
