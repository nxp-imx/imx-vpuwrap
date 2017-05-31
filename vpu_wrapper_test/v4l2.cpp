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

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <list>
#include "v4l2.h"
#include "log.h"

//#define TEST_CODA

#define ADDED_BUFFER_NUM 3
#define COMPRESS_BUFFER_SIZE (512*1024)
#define CAN_READ_EVENT(pfd) (pfd.revents & POLLPRI)
#define CAN_READ_BUFFER(pfd) (V4L2_TYPE_IS_OUTPUT(type) ? \
    pfd.revents & POLLOUT : pfd.revents & POLLIN)
#define TIMEVAL2TIMEMS(tv) ((tv).tv_sec * 1000 + (tv).tv_usec / 1000)
#define TIMEMS2TIMEVAL(t,tv) (tv).tv_sec = t / 1000; \
                              (tv).tv_usec = (t - (tv).tv_sec * 1000) * 1000; \

static std::list<int64> dts_list;

v4l2::v4l2 (int fd, int v4l2_buf_type)
{
  mfd = dup (fd);
  type = v4l2_buf_type;

  pfd.fd = mfd;
  pfd.events = POLLERR | POLLNVAL | POLLHUP;
  pfd.revents = 0;
  if (V4L2_TYPE_IS_OUTPUT(type))
    pfd.events |= POLLOUT;
  else
    pfd.events |= POLLIN | POLLPRI;
  mstate = V4L2_STATE_STOP;
  mformat.yuv_format_v4l2 = 0;
  antispate_pts = 0;
  pts_interval = 33333333;
  use_dts = false;
  last_send_pts = -1;
}

v4l2::~v4l2 ()
{
  pthread_mutex_destroy(&stream_lock);
  if (mfd)
    close (mfd);
}

V4L2_STATE v4l2::get_state ()
{
  return mstate;
}

bool v4l2::set_format (Format *format)
{
  struct v4l2_format mv4l2_format = {0};
  struct v4l2_crop mv4l2_crop = {0};
  struct v4l2_streamparm mv4l2_parm = {0};
  int i;
  
  CHECK_NULL (format);
  mv4l2_format.type = type;
#ifndef TEST_CODA
  if (ioctl (mfd, VIDIOC_G_FMT, &mv4l2_format) < 0) {
    LOG_ERROR ("VIDIOC_G_FMT fail: %d\n", errno);
    return false;
  }
#endif
  if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
    if ((format->codec_type == CODEC_TYPE_ENCODER && V4L2_TYPE_IS_OUTPUT(type))
        || (format->codec_type == CODEC_TYPE_DECODER && !V4L2_TYPE_IS_OUTPUT(type))) {
      mv4l2_format.fmt.pix_mp.pixelformat = v4l2_fourcc (format->yuv_format[0],
          format->yuv_format[1], format->yuv_format[2], format->yuv_format[3]);
    } else {
      mv4l2_format.fmt.pix_mp.pixelformat = v4l2_fourcc (format->format[0],
          format->format[1], format->format[2], format->format[3]);
      for (i = 0; i < format->plane_num; i++) {
        mv4l2_format.fmt.pix_mp.plane_fmt[i].sizeimage = COMPRESS_BUFFER_SIZE;
      }
    }
    mv4l2_format.fmt.pix_mp.width = format->width;
    mv4l2_format.fmt.pix_mp.height = format->height;
    mv4l2_format.fmt.pix_mp.num_planes = format->plane_num;
    for (i = 0; i < format->plane_num; i++) {
      mv4l2_format.fmt.pix_mp.plane_fmt[i].bytesperline = format->stride[i];
    }
  } else {
    if ((format->codec_type == CODEC_TYPE_ENCODER && V4L2_TYPE_IS_OUTPUT(type))
        || (format->codec_type == CODEC_TYPE_DECODER && !V4L2_TYPE_IS_OUTPUT(type))) {
      mv4l2_format.fmt.pix.pixelformat = v4l2_fourcc (format->yuv_format[0],
          format->yuv_format[1], format->yuv_format[2], format->yuv_format[3]);
    } else {
      mv4l2_format.fmt.pix.pixelformat = v4l2_fourcc (format->format[0],
          format->format[1], format->format[2], format->format[3]);
      mv4l2_format.fmt.pix.sizeimage = COMPRESS_BUFFER_SIZE;
    }
    mv4l2_format.fmt.pix.width = format->width;
    mv4l2_format.fmt.pix.height = format->height;
    mv4l2_format.fmt.pix.bytesperline = format->stride[0];
  }
  if (ioctl (mfd, VIDIOC_S_FMT, &mv4l2_format) < 0) {
    LOG_ERROR ("VIDIOC_S_FMT fail: %d:%s\n", errno, strerror(errno));
    return false;
  }
  memset (&mv4l2_format, 0x00, sizeof (struct v4l2_format));
  mv4l2_format.type = type;
  if (ioctl (mfd, VIDIOC_G_FMT, &mv4l2_format) < 0) {
    LOG_ERROR ("VIDIOC_G_FMT fail: %d\n", errno);
    return false;
  }
  if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
    format->yuv_format_v4l2 = mv4l2_format.fmt.pix_mp.pixelformat;
    format->width = mv4l2_format.fmt.pix_mp.width;
    format->height = mv4l2_format.fmt.pix_mp.height;
    format->plane_num = mv4l2_format.fmt.pix_mp.num_planes;
    for (i = 0; i < format->plane_num; i++) {
      format->stride[i] = mv4l2_format.fmt.pix_mp.plane_fmt[i].bytesperline;
      format->image_size[i] = mv4l2_format.fmt.pix_mp.plane_fmt[i].sizeimage;
    }
  }else{
    format->yuv_format_v4l2 = mv4l2_format.fmt.pix.pixelformat;
    format->width = mv4l2_format.fmt.pix.width;
    format->height = mv4l2_format.fmt.pix.height;
    format->plane_num = 1;
    format->stride[0] = mv4l2_format.fmt.pix.bytesperline;
    format->image_size[0] = mv4l2_format.fmt.pix.sizeimage;
  }
  LOG_INFO ("pixelformat: %c%c%c%c\n", format->yuv_format_v4l2 & 0xff,
      (format->yuv_format_v4l2 >> 8) & 0xff, (format->yuv_format_v4l2 >> 16) 
      & 0xff,(format->yuv_format_v4l2 >> 24) & 0xff);
  LOG_INFO ("width: %d height: %d plane_num: %d stride: %d image_size: %d\n",
      format->width, format->height, format->plane_num, format->stride[0],
      format->image_size[0]);

  if (format->codec_type == CODEC_TYPE_ENCODER && V4L2_TYPE_IS_OUTPUT(type)) {
    mv4l2_crop.type = type;
    mv4l2_crop.c.left = format->crop_left;
    mv4l2_crop.c.top = format->crop_top;
    mv4l2_crop.c.width = format->crop_width;
    mv4l2_crop.c.height = format->crop_height;
    if (ioctl (mfd, VIDIOC_S_CROP, &mv4l2_crop) < 0) {
      LOG_ERROR ("VIDIOC_S_CROP fail: %d\n", errno);
    }
    memset (&mv4l2_crop, 0, sizeof (struct v4l2_crop));
    mv4l2_crop.type = type;
    if (ioctl (mfd, VIDIOC_G_CROP, &mv4l2_crop) < 0) {
      LOG_ERROR ("VIDIOC_G_CROP fail: %d\n", errno);
    }

    mv4l2_parm.type = type;
    mv4l2_parm.parm.capture.timeperframe.numerator = format->fps_n;
    mv4l2_parm.parm.capture.timeperframe.denominator = format->fps_d;
    if (ioctl (mfd, VIDIOC_S_PARM, &mv4l2_parm) < 0) {
      LOG_ERROR ("VIDIOC_S_PARM fail: %d\n", errno);
    }
    if (ioctl (mfd, VIDIOC_G_PARM, &mv4l2_parm) < 0) {
      LOG_ERROR ("VIDIOC_G_PARM fail: %d\n", errno);
    }
  }

  if (format->fps_n && format->fps_d)
    pts_interval = format->fps_d * 1000000000ll / format->fps_n;
  mformat = *format;
  mstate = V4L2_STATE_SETUP;

  return true;
}

bool v4l2::get_format (Format * format)
{
  struct v4l2_format mv4l2_format = {0};
  struct v4l2_crop mv4l2_crop = {0};
  struct v4l2_control mv4l2_control = {0};
  struct v4l2_streamparm mv4l2_parm = {0};
  int i;
 
  CHECK_NULL (format);
#ifndef TEST_CODA
  if (!mformat.yuv_format_v4l2) {
retry:
    ppoll (&pfd, 1, NULL, NULL);
    LOG_DEBUG ("ppoll return value: %d\n", pfd.revents);
    if (pfd.revents == POLLERR) {
      usleep(1000000);
      goto retry;
    }

    if (!CAN_READ_EVENT (pfd)) {
      LOG_ERROR ("driver should output SOURCE_CHANGE event.\n");
      return false;
    }
    if (dqevent () != V4L2_RET_SOURCE_CHANGE) {
      LOG_ERROR ("The first event isn't SOURCE_CHANGE.\n");
      return false;
    }
  }
#endif

  mv4l2_format.type = type;
  if (ioctl (mfd, VIDIOC_G_FMT, &mv4l2_format) < 0) {
    LOG_ERROR ("VIDIOC_G_FMT fail: %d\n", errno);
    return false;
  }

  if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
    format->yuv_format_v4l2 = mv4l2_format.fmt.pix_mp.pixelformat;
    format->width = mv4l2_format.fmt.pix_mp.width;
    format->height = mv4l2_format.fmt.pix_mp.height;
    format->plane_num = mv4l2_format.fmt.pix_mp.num_planes;
    for (i = 0; i < format->plane_num; i++) {
      format->stride[i] = mv4l2_format.fmt.pix_mp.plane_fmt[i].bytesperline;
      format->image_size[i] = mv4l2_format.fmt.pix_mp.plane_fmt[i].sizeimage;
    }
  }else{
    format->yuv_format_v4l2 = mv4l2_format.fmt.pix.pixelformat;
    format->width = mv4l2_format.fmt.pix.width;
    format->height = mv4l2_format.fmt.pix.height;
    format->plane_num = 1;
    format->stride[0] = mv4l2_format.fmt.pix.bytesperline;
    format->image_size[0] = mv4l2_format.fmt.pix.sizeimage;
  }
  LOG_INFO ("pixelformat: %c%c%c%c\n", format->yuv_format_v4l2 & 0xff,
      (format->yuv_format_v4l2 >> 8) & 0xff, (format->yuv_format_v4l2 >> 16) 
      & 0xff,(format->yuv_format_v4l2 >> 24) & 0xff);
  LOG_INFO ("width: %d height: %d plane_num: %d stride: %d image_size: %d\n",
      format->width, format->height, format->plane_num, format->stride[0],
      format->image_size[0]);

  mv4l2_crop.type = type;
  if (ioctl (mfd, VIDIOC_G_CROP, &mv4l2_crop) < 0) {
    LOG_WARNING ("VIDIOC_G_CROP fail: %d\n", errno);
    mv4l2_crop.c.left = 0;
    mv4l2_crop.c.top = 0;
    mv4l2_crop.c.width = format->width;
    mv4l2_crop.c.height = format->height;
  }
  format->crop_left = mv4l2_crop.c.left;
  format->crop_top = mv4l2_crop.c.top;
  format->crop_width = mv4l2_crop.c.width;
  format->crop_height = mv4l2_crop.c.height;

  mv4l2_control.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
  if (ioctl(mfd, VIDIOC_G_CTRL, &mv4l2_control) < 0) {
    LOG_WARNING ("VIDIOC_G_CTRL fail: %d: %s\n", errno, strerror (errno));
    mv4l2_control.value = 0;
  }
  format->min_buffers = mv4l2_control.value;// + ADDED_BUFFER_NUM;
  LOG_INFO ("v4l2 min buffers: %d, added number: %d\n", mv4l2_control.value, ADDED_BUFFER_NUM);

  mv4l2_parm.type = type;
  if (ioctl (mfd, VIDIOC_G_PARM, &mv4l2_parm) < 0) {
    LOG_WARNING ("VIDIOC_G_PARM fail: %d\n", errno);
    mv4l2_parm.parm.capture.timeperframe.numerator = 30;
    mv4l2_parm.parm.capture.timeperframe.denominator = 1;
  } else {
    format->fps_n = mv4l2_parm.parm.capture.timeperframe.numerator;
    format->fps_d = mv4l2_parm.parm.capture.timeperframe.denominator;
  }
 
  return true;
}

bool v4l2::start ()
{
  struct v4l2_requestbuffers buf_req = {0};
  struct v4l2_event_subscription sub = {0};
  int i, j;

  pthread_mutex_destroy(&stream_lock);
  pthread_mutex_init(&stream_lock, NULL);

#ifndef TEST_CODA
  sub.type = V4L2_EVENT_SOURCE_CHANGE;
  if (ioctl(mfd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
    LOG_ERROR("VIDIOC_SUBSCRIBE_EVENT fail: %d: %s\n", errno, strerror (errno));
    return false;
  }
#endif
 
  sub.type = V4L2_EVENT_EOS;
  if (ioctl(mfd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
    LOG_ERROR("VIDIOC_SUBSCRIBE_EVENT fail: %d: %s\n", errno, strerror (errno));
    return false;
  }
  buf_req.type = type;
  if (mformat.min_buffers == 0)
    mformat.min_buffers = ADDED_BUFFER_NUM;
  buf_req.count = mformat.min_buffers;
  if ((mformat.codec_type == CODEC_TYPE_DECODER && V4L2_TYPE_IS_OUTPUT(type))
      || (mformat.codec_type == CODEC_TYPE_ENCODER && !V4L2_TYPE_IS_OUTPUT(type))) {
    mformat.memory_mode = V4L2_MEMORY_MMAP;
  }
  buf_req.memory = mformat.memory_mode;
  if (ioctl(mfd, VIDIOC_REQBUFS, &buf_req) < 0) {
    LOG_ERROR("Request %d buffers failed\n", mformat.min_buffers);
    return false;
  }
  query_num = 0;
  bstreamon = false;
  for (i = 0; i < mformat.min_buffers; i ++) {
    for (j = 0; j < mformat.plane_num; j++) {
      mv4l2_buffers_addr[i][j] = NULL;
    }
  }
  bv4l2_buffer_ready = false;
  mstate = V4L2_STATE_RUNNING;

  return true;
}

V4L2_RET v4l2::query_buffer (Buffer *buf)
{
  int i, j;

  if (!bv4l2_buffer_ready) {
    if (mformat.memory_mode == V4L2_MEMORY_MMAP) {
      for (i = 0; i < mformat.min_buffers; i ++) {
        mv4l2_buffers[i].type = type;
        mv4l2_buffers[i].memory = mformat.memory_mode;
        mv4l2_buffers[i].index = i;
        if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
          mv4l2_buffers[i].length = mformat.plane_num;
          mv4l2_buffers[i].m.planes = mv4l2_planes[i];
        }

        if (ioctl (mfd, VIDIOC_QUERYBUF, &mv4l2_buffers[i]) < 0) {
          LOG_ERROR ("VIDIOC_QUERYBUF fail: %d\n", errno);
          return V4L2_RET_FAILURE;
        }

        if (!V4L2_TYPE_IS_MULTIPLANAR (type)) {
          mv4l2_planes[i][0].length = mv4l2_buffers[i].length;
          mv4l2_planes[i][0].m.mem_offset = mv4l2_buffers[i].m.offset;
        }

        for (j = 0; j < mformat.plane_num; j++) {
          LOG_DEBUG ("planes length: %d, offset: %d\n", mv4l2_planes[i][j].length,
              mv4l2_planes[i][j].m.mem_offset);
          mv4l2_buffers_addr[i][j] = (char *)mmap(NULL,
              mv4l2_planes[i][j].length, PROT_READ | PROT_WRITE, MAP_SHARED,
              mfd, mv4l2_planes[i][j].m.mem_offset);
          if(MAP_FAILED == mv4l2_buffers_addr[i][j]) {
            LOG_ERROR ("mmap fail: %d: %s", errno, strerror (errno));
            return V4L2_RET_FAILURE;
          }
        }
      }
    } else {
      for (i = 0; i < mformat.min_buffers; i ++) {
        mv4l2_buffers[i].type = type;
        mv4l2_buffers[i].memory = mformat.memory_mode;
        mv4l2_buffers[i].index = i;
        if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
          mv4l2_buffers[i].length = mformat.plane_num;
          mv4l2_buffers[i].m.planes = mv4l2_planes[i];
          for (j = 0; j < mformat.plane_num; j++) {
            mv4l2_planes[i][j].length = mformat.image_size[j];;
          }
        } else {
          mv4l2_buffers[i].length = mformat.image_size[0];
        }
      }
    }
  }
  bv4l2_buffer_ready = true;

  buf->plane_num = mformat.plane_num;
  for (i = 0; i < mformat.plane_num; i++) {
    if (mformat.memory_mode == V4L2_MEMORY_MMAP) {
      buf->data[i] = mv4l2_buffers_addr[query_num][i];
      buf->alloc_size[i] = mv4l2_planes[query_num][i].length;
      buf->v4l2_index = mv4l2_buffers[query_num].index;
    } else {
      buf->data[i] = NULL;
      buf->alloc_size[i] = mv4l2_planes[query_num][i].length;
      buf->v4l2_index = mv4l2_buffers[query_num].index;
    }
    LOG_DEBUG ("alloc_size: %d\n", buf->alloc_size[i]);
  }

  query_num++;
  if (query_num >= mformat.min_buffers)
    mstate = V4L2_STATE_BUFFER_READY;
  return V4L2_RET_NONE;
}

V4L2_RET v4l2::dqevent ()
{
  V4L2_RET ret = V4L2_RET_NONE;
  struct v4l2_event evt = {0};
  if (ioctl (mfd, VIDIOC_DQEVENT, &evt) < 0) {
    LOG_ERROR ("VIDIOC_DQEVENT fail: %d\n", errno);
    return V4L2_RET_FAILURE;
  }
  switch (evt.type)
  {
    case V4L2_EVENT_SOURCE_CHANGE:
      ret = V4L2_RET_SOURCE_CHANGE;
      break;
    case V4L2_EVENT_EOS:
      ret = V4L2_RET_EOS;
      break;
    default:
      break;
  }
  return ret;
}

V4L2_RET v4l2::dqbuffer (Buffer *buf)
{
  struct v4l2_buffer buffer = { 0 };
  struct v4l2_plane planes[VIDEO_MAX_PLANES] = { {0} };
  int i;

  pthread_mutex_lock(&stream_lock);
  buffer.type = type;
  buffer.memory = mformat.memory_mode;
  if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
    buffer.length = mformat.plane_num;
    buffer.m.planes = planes;
  }
  if (ioctl (mfd, VIDIOC_DQBUF, &buffer) < 0) {
    LOG_ERROR ("VIDIOC_DQBUF fail: %d: %s\n", errno, strerror (errno));
    pthread_mutex_unlock (&stream_lock);
    return V4L2_RET_FAILURE;
  }

  for (i = 0; i < mformat.plane_num; i ++) {
    if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
      buf->size[i] = planes[i].bytesused; 
    } else {
      buf->size[0] = buffer.bytesused;
    }
    if (mformat.memory_mode == V4L2_MEMORY_MMAP) {
      buf->data[i] = mv4l2_buffers_addr[buffer.index][i];
    } else if (mformat.memory_mode == V4L2_MEMORY_USERPTR) {
      if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
        buf->data[i] = (char *)mv4l2_planes[buffer.index][i].m.userptr;
      }
    } if (mformat.memory_mode == V4L2_MEMORY_DMABUF) {
      if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
        //buf->data[i] = mv4l2_planes[buffer.index][i].m.fd;
      }
    }

  }
  buf->pts = TIMEVAL2TIMEMS (buffer.timestamp);
  if (mformat.codec_type == CODEC_TYPE_DECODER && !V4L2_TYPE_IS_OUTPUT(type)) {
    LOG_DEBUG ("v4l2 output pts: %lld\n", buf->pts);
#if 0
    if (buf->pts == 0) {
      buf->pts = antispate_pts / 1000000;
      LOG_WARNING ("invalid pts, prediction.\n");
    } else {
      antispate_pts = buf->pts * 1000000;
    }
    antispate_pts += pts_interval;
    if (buf->pts < last_send_pts || use_dts) {
      use_dts = true;
      buf->pts = dts_list.front ();
      LOG_WARNING ("reorder wrong, use DTS:%lld list size: %d.\n", buf->pts,
          dts_list.size ());
    }
#endif
    //FIXME: needn't this.
    if (dts_list.size () > 0)
      dts_list.pop_front ();
    last_send_pts = buf->pts;
    LOG_DEBUG ("output pts: %lld\n", buf->pts);
  }
  buf->v4l2_index = buffer.index;
  LOG_DEBUG ("dequeued buffer type: %d index: %d size: %d timestamp: %lld\n",
      type, buf->v4l2_index, buf->size[0], buf->pts);
  pthread_mutex_unlock (&stream_lock);
  if (buf->size[0] == 0) {
    LOG_DEBUG ("received zero size buffer, return EOS.\n");
    return V4L2_RET_EOS;
  }

  return V4L2_RET_NONE;
}

V4L2_RET v4l2::get_buffer (Buffer *buf)
{
  struct timespec timeout;
  if (mstate < V4L2_STATE_BUFFER_READY) {
    return query_buffer (buf);
  }

  //FIXME:workaround for write can interrupt ppoll.
  timeout.tv_sec = 0;
  timeout.tv_nsec = 500000000;
  ppoll (&pfd, 1, &timeout, NULL);
  if (pfd.revents == POLLERR) {
    char tembuf[1];
    read (mfd, tembuf, 1);
    usleep(1000000);
  }
  LOG_DEBUG ("ppoll return value: %d\n", pfd.revents);

  if (CAN_READ_EVENT (pfd)) {
    LOG_DEBUG ("can read event from driver.\n");
    return dqevent ();
  }

  if (CAN_READ_BUFFER (pfd)) {
    LOG_DEBUG ("can read buffer from driver.\n");
    return dqbuffer (buf);
  }

  return V4L2_RET_FAILURE;
}

bool v4l2::put_buffer (Buffer *buf)
{
  int i;

  pthread_mutex_lock (&stream_lock);
  if (V4L2_TYPE_IS_MULTIPLANAR (type)) {
    for (i = 0; i < mformat.plane_num; i ++) {
      mv4l2_planes[buf->v4l2_index][i].bytesused = buf->size[i]; 
      if (mformat.memory_mode == V4L2_MEMORY_USERPTR) {
        mv4l2_planes[buf->v4l2_index][i].m.userptr = (unsigned long)buf->data[i]; 
      } else if (mformat.memory_mode == V4L2_MEMORY_DMABUF) {
        mv4l2_planes[buf->v4l2_index][i].m.fd = (signed long)buf->data[i]; 
      }
    }
  } else {
    mv4l2_buffers[buf->v4l2_index].bytesused = buf->size[0];
    if (mformat.memory_mode == V4L2_MEMORY_USERPTR) {
      mv4l2_buffers[buf->v4l2_index].m.userptr = (unsigned long)buf->data[i]; 
    } else if (mformat.memory_mode == V4L2_MEMORY_DMABUF) {
      mv4l2_buffers[buf->v4l2_index].m.fd = (signed long)buf->data[i]; 
    }
  }
  mv4l2_buffers[buf->v4l2_index].timestamp.tv_sec = 0;
  mv4l2_buffers[buf->v4l2_index].timestamp.tv_usec = 0;
  if (buf->pts > 0) {
    TIMEMS2TIMEVAL(buf->pts, mv4l2_buffers[buf->v4l2_index].timestamp);
  }
  if (mformat.codec_type == CODEC_TYPE_DECODER && V4L2_TYPE_IS_OUTPUT(type)) {
    dts_list.push_back (buf->dts);
  }
  LOG_DEBUG ("type: %d queued buffer size: %d timestamp: %lld dts: %lld\n",
      type, buf->size[0], buf->pts, buf->dts);
  if (buf->size[0] == 0 && V4L2_TYPE_IS_OUTPUT(type)) {
    struct v4l2_decoder_cmd cmd = {0};
#if 0
    cmd.cmd = V4L2_DEC_CMD_STOP;
    cmd.flags = V4L2_DEC_CMD_STOP_IMMEDIATELY;
    if (ioctl (mfd, VIDIOC_DECODER_CMD, &cmd) < 0) {
      LOG_ERROR ("VIDIOC_DECODER_CMD fail: %d: %s\n", errno, strerror (errno));
      pthread_mutex_unlock (&stream_lock);
      return false;
    }
    pthread_mutex_unlock (&stream_lock);
    return true;
#endif
  }
  if (ioctl (mfd, VIDIOC_QBUF, &(mv4l2_buffers[buf->v4l2_index])) < 0) {
    LOG_ERROR ("VIDIOC_QBUF fail: %d: %s\n", errno, strerror (errno));
    pthread_mutex_unlock (&stream_lock);
    return false;
  }
  if (buf->size[0] == 0 && V4L2_TYPE_IS_OUTPUT(type)) {
      usleep(1000000);
  }

  if (!bstreamon) {
    if (ioctl (mfd, VIDIOC_STREAMON, &type) < 0) {
      LOG_ERROR ("VIDIOC_STREAMON fail: %d: %s\n", errno, strerror (errno));
      pthread_mutex_unlock (&stream_lock);
      return false;
    }
    bstreamon = true;
  }
  pthread_mutex_unlock (&stream_lock);

  return true;
}

bool v4l2::flush()
{
  write (mfd, "W", 1);
  pthread_mutex_lock (&stream_lock);
  if (ioctl (mfd, VIDIOC_STREAMOFF, &type) < 0) {
    LOG_ERROR ("VIDIOC_STREAMOFF fail: %d\n", errno);
    pthread_mutex_unlock (&stream_lock);
    return false;
  }
  bstreamon = false;
  mstate = V4L2_STATE_RUNNING;
  query_num = 0;
  dts_list.clear ();
  pthread_mutex_unlock (&stream_lock);

  return true;
}

bool v4l2::stop ()
{
  struct v4l2_event_subscription sub = {0};
  int i, j;

  write (mfd, "W", 1);
  pthread_mutex_lock (&stream_lock);
  if (ioctl (mfd, VIDIOC_STREAMOFF, &type) < 0) {
    LOG_ERROR ("VIDIOC_STREAMOFF fail: %d\n", errno);
    pthread_mutex_unlock (&stream_lock);
    return false;
  }

  if (mformat.memory_mode == V4L2_MEMORY_MMAP) {
    for (i = 0; i < mformat.min_buffers; i ++) {
      for (j = 0; j < mformat.plane_num; j++) {
         munmap (mv4l2_buffers_addr[i][j], mv4l2_planes[i][j].length);
         mv4l2_buffers_addr[i][j] = NULL;
      }
    }
  }

  sub.type = V4L2_EVENT_ALL;
  if (ioctl(mfd, VIDIOC_UNSUBSCRIBE_EVENT, &sub) < 0) {
    LOG_ERROR ("VIDIOC_UNSUBSCRIBE_EVENT fail %d: %s\n", errno, strerror (errno));
  }

  bstreamon = false;
  mstate = V4L2_STATE_STOP;
  query_num = 0;
  dts_list.clear ();
  pthread_mutex_unlock (&stream_lock);

  return true;
}

