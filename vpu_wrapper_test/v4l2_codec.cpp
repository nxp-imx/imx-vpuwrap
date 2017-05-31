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
#include <signal.h>
#include "v4l2_codec.h"
#include "log.h"

v4l2_codec::v4l2_codec (int fd)
{
  mfd = dup (fd);
}

v4l2_codec::~v4l2_codec ()
{
  if (mfd)
    close (mfd);
}

bool v4l2_codec::set_input (input *input)
{
  CHECK_NULL (input);
  minput = input;
  return true;
}

bool v4l2_codec::set_output (output *output)
{
  CHECK_NULL (output);
  moutput = output;
  return true;
}

bool v4l2_codec::set_format (Format *format)
{
  CHECK_NULL (format);
  mformat = *format;
  return true;
}

static void *input_thread_wrap (void *arg)
{
  v4l2_codec *mv4l2_codec = 	(v4l2_codec *)arg;
  return mv4l2_codec->input_thread ();
}

void *v4l2_codec::input_thread ()
{
  V4L2_RET ret;
  Buffer buf = {0};
  while (!bstop) {
    if (bseek) {
      minput->set_position (seek_portion, seek_timems);
      mv4l2_output->flush ();
      mv4l2_capture->flush ();
      moutput->flush ();
      bseek = false;
    }
    ret = mv4l2_output->get_buffer (&buf);
    if (ret != V4L2_RET_NONE) {
      continue;
    }
    minput->get_buffer (&buf);
    mv4l2_output->put_buffer (&buf);
  }
  LOG_DEBUG ("input thread exit.\n");
  return NULL;
}
  
void *output_thread_wrap (void * arg)
{
  v4l2_codec * mv4l2_codec = 	(v4l2_codec * )arg;
  return mv4l2_codec->output_thread ();
}

void *v4l2_codec::output_thread ()
{
  V4L2_RET ret;
  Buffer buf = {0};
  while (!bstop) {
    if (mv4l2_capture->get_state () == V4L2_STATE_STOP) {
      if (mformat.codec_type == CODEC_TYPE_DECODER)
        mv4l2_capture->get_format (&mformat);
      mv4l2_capture->set_format (&mformat);
      moutput->set_format (&mformat);
      mv4l2_capture->start ();
    }
    ret = mv4l2_capture->get_buffer (&buf);
    if (ret == V4L2_RET_SOURCE_CHANGE) {
      LOG_INFO ("v4l2 report resolution change.\n");
      mv4l2_capture->stop ();
      continue;
    } else if (ret == V4L2_RET_EOS
        || (mformat.count != INVALID_PARAM && mcount > mformat.count)) {
      if (mformat.seek_portion != INVALID_PARAM
          || mformat.seek_timems != INVALID_PARAM) {
        seek (mformat.seek_portion, mformat.seek_timems);
        mformat.seek_portion = INVALID_PARAM;
        mformat.seek_timems = INVALID_PARAM;
        mformat.count = INVALID_PARAM;
      } else {
        sigval value;
        bstop = true;
        sigqueue (getpid (), SIGUSR1, value);
        LOG_INFO ("EOS, exit\n");
        continue;
      }
    } if (ret == V4L2_RET_FAILURE) {
      continue;
    }
    moutput->put_buffer (&buf);
    mv4l2_capture->put_buffer (&buf);
    mcount ++;
    LOG_DEBUG ("video codec count: %d:%d\n", mcount, mformat.count);
  }
  LOG_DEBUG ("output thread exit.\n");
  return NULL;
}

bool v4l2_codec::start ()
{
  int type_output, type_capture;

  if (mformat.codec_mode == CODEC_MODE_MULTI_PLANE) {
    type_output = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    type_capture = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  } else {
    type_output = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    type_capture = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  }
  mv4l2_output = new v4l2 (mfd, type_output);
  if (!mv4l2_output) {
    LOG_ERROR ("create v4l2 output fail.\n");
    return false;
  }
  mv4l2_capture = new v4l2 (mfd, type_capture);
  if (!mv4l2_capture) {
    LOG_ERROR ("create v4l2 capture fail.\n");
    return false;
  }

  minput->get_format (&mformat);
  if (!mv4l2_output->set_format (&mformat)) {
    LOG_ERROR ("v4l2 output set format fail.\n");
    return false;
  }
  if (!mv4l2_output->start ()) {
    LOG_ERROR ("v4l2 output start fail.\n");
    return false;
  }
  
  mcount = 0;
  pthread_create(&(input_thread_id), NULL, input_thread_wrap, this);
  pthread_create(&(output_thread_id), NULL, output_thread_wrap, this);

  return true;
}

bool v4l2_codec::seek (int portion, int64 timems)
{
  seek_portion = portion;
  seek_timems = timems;
  bseek = true;
  mcount = 0;
  return true;
}

bool v4l2_codec::stop ()
{
  bstop = true;
  LOG_DEBUG ("v4l2_codec flush\n");
  mv4l2_output->flush ();
  LOG_DEBUG ("mv4l2_output flushed\n");
  pthread_join (input_thread_id, NULL);
  LOG_DEBUG ("input_thread stoped\n");
  mv4l2_capture->flush ();
  LOG_DEBUG ("mv4l2_capture flushed\n");
  pthread_join (output_thread_id, NULL);
  LOG_DEBUG ("output_thread stoped\n");
  LOG_DEBUG ("v4l2_codec stop\n");
  mv4l2_output->stop ();
  LOG_DEBUG ("mv4l2_output stoped\n");
  mv4l2_capture->stop ();
  LOG_DEBUG ("mv4l2_capture stoped\n");
 
  return true;
}
