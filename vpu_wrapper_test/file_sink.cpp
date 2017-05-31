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

#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "file_sink.h"
#include "log.h"

file_sink::file_sink ()
{
  fp_data = NULL;
  fp_meta = NULL;
}

file_sink::~file_sink ()
{
  if (fp_data){
    fclose (fp_data);
    LOG_DEBUG("file_sink::~file_sink \n");
  }

  if (fp_meta)
    fclose (fp_meta);

}

bool file_sink::set_format_sink (Format *format)
{
  char output_file_name[32];
  CHECK_NULL (format);
  mformat = *format;

  if (fp_data)
    return true;

  if (!format->output_file && format->width && format->height
      && format->yuv_format_v4l2) {
    char *subfix;
    sprintf (output_file_name, "video_codec_output_%dx%d.%c%c%c%c",
        format->width, format->height, format->yuv_format_v4l2 & 0xff,
        (format->yuv_format_v4l2 >> 8) & 0xff, (format->yuv_format_v4l2 >> 16) 
        & 0xff,(format->yuv_format_v4l2 >> 24) & 0xff);
    subfix = strrchr (output_file_name, '.');
    if (subfix) {
      char *pch = subfix;
      while (*pch != '\0') {
        *pch = tolower (*pch);
        pch++; 
      }   
    }
    format->output_file = output_file_name;
    LOG_INFO ("unit test output file name: %s\n", format->output_file);
  }

  if (format->output_file) {
    write_size = 0;
    fp_data = fopen(format->output_file, "w");
    LOG_DEBUG("file_sink::  fopen \n");

    if (!fp_data) {
      LOG_ERROR ("open data file fail: %s", format->output_file);
      return false;
    }
  }

  if (format->output_meta) {
    fp_meta = fopen(format->output_meta, "w");
    if (!fp_meta) {
      LOG_ERROR ("open meta file fail: %s", format->output_meta);
      return false;
    }
  }

  return true;
}

bool file_sink::put_buffer_sink (Buffer *buf)
{
  int size, i;
  CHECK_NULL (buf);

  for (i = 0; i < mformat.plane_num; i ++) {
    size = fwrite (buf->data[i], 1, buf->size[i], fp_data);
    if (size != buf->size[i]) {
      LOG_ERROR ("fwrite fail: %d\n", errno);
      return false;
    }
    LOG_DEBUG ("fwrite size: %d\n", size);
  }
  (void)fflush(fp_data);

  return true;
}
