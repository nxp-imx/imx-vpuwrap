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

#include <stdlib.h>
#include "analysis.h"
#include "log.h"

#define TIMESPEC2TIMEMS(tv) ((tv).tv_sec * 1000 + (tv).tv_nsec / 1000000)

analysis::analysis ()
{
  fp_data = NULL;
  fp_meta = NULL;
  frame_count = 0;
}

analysis::~analysis ()
{
  if (fp_data)
    fclose (fp_data);
  if (fp_meta)
    fclose (fp_meta);
}

bool analysis::set_format (Format *format)
{
  CHECK_NULL (format);
  mformat = *format;

  if (fp_data)
    fclose (fp_data);
  if (fp_meta)
    fclose (fp_meta);

  if (format->output_file_ref) {
    fp_data = fopen(format->output_file_ref, "r");
    if (!fp_data) {
      LOG_ERROR ("open data file fail: %s", format->input_file);
      return false;
    }
  }

  if (format->output_meta_ref) {
    fp_meta = fopen(format->output_meta_ref, "r");
    if (!fp_meta) {
      LOG_ERROR ("open meta file fail: %s", format->input_meta);
      return false;
    }
  }
 
  clock_gettime(CLOCK_MONOTONIC, &start);

  return true;
}

bool analysis::put_buffer (Buffer *buf)
{
  int64 pts, dts, offset;
  struct timespec now;
  CHECK_NULL (buf);
  if (fp_meta && buf->size[0]) {
    if (fscanf (fp_meta, "PTS: %lld\n", &pts) > 0) {
      pts /= 1000000;
      LOG_DEBUG ("PTS: %lld or %lld\n", buf->pts, pts);
      if (abs (buf->pts - pts) > 5) {
        LOG_ERROR ("Seems PTS wrong: %lld or %lld\n", buf->pts, pts);
      }
    }
  }
  clock_gettime(CLOCK_MONOTONIC, &now);
  frame_count ++;
  LOG_INFO ("frame count: %d  consume time: %d\n", frame_count,
      TIMESPEC2TIMEMS(now) - TIMESPEC2TIMEMS(pre_time));
  pre_time = now;
 
  return true;
}

bool analysis::get_result ()
{
  struct timespec end;
  clock_gettime(CLOCK_MONOTONIC, &end);
  LOG_INFO ("decode fps: %d\n", frame_count * 1000 / (TIMESPEC2TIMEMS(end) -
        TIMESPEC2TIMEMS(start)));
  return true;
}
