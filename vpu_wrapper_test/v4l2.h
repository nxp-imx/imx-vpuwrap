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

#ifndef __V4L2_H__
#define __V4L2_H__

#include <poll.h>
#include <pthread.h>
#include <linux/videodev2.h>
#include "common.h"

typedef enum {
  V4L2_STATE_STOP,
  V4L2_STATE_SETUP,
  V4L2_STATE_RUNNING,
  V4L2_STATE_BUFFER_READY
} V4L2_STATE;

typedef enum {
  V4L2_RET_NONE,
  V4L2_RET_FAILURE,
  V4L2_RET_SOURCE_CHANGE,
  V4L2_RET_EOS
} V4L2_RET;

class v4l2 {
  public:
    v4l2 (int fd, int v4l2_buf_type);
    virtual ~v4l2 ();
    V4L2_STATE get_state ();
    bool set_format (Format *format);
    bool get_format (Format *format);
    bool start ();
    V4L2_RET get_buffer (Buffer *buf);
    bool put_buffer (Buffer *buf);
    bool flush ();
    bool stop ();
  private:
    int mfd;
    int type;
    V4L2_RET query_buffer (Buffer *buf);
    V4L2_RET dqbuffer (Buffer *buf);
    V4L2_RET dqevent ();
    struct pollfd pfd;
    pthread_mutex_t stream_lock;
    Format mformat;
    struct v4l2_buffer mv4l2_buffers[MAX_V4L2_BUFFERS];
    struct v4l2_plane mv4l2_planes[MAX_V4L2_BUFFERS][MAX_V4L2_PLANES];
    char * mv4l2_buffers_addr[MAX_V4L2_BUFFERS][MAX_V4L2_PLANES];
    int query_num;
    bool bstreamon;
    bool bv4l2_buffer_ready;
    V4L2_STATE mstate;
    int64 antispate_pts;
    int pts_interval;
    bool use_dts;
    int64 last_send_pts;
};

#endif
