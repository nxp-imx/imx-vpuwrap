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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "file_input.h"
#include "log.h"

file_input::file_input ()
{
  fp_data = NULL;
  fp_meta = NULL;
  bfirst_buffer = true;
  duration = 0;
  bSeek = false;
  seek_offset = 0;
  seek_size = 0;
  seek_pts = 0;
  seek_dts = 0;
}

file_input::~file_input ()
{
  if (fp_data)
    fclose (fp_data);
  if (fp_meta)
    fclose (fp_meta);
}

bool file_input::set_format (Format * format)
{
  struct stat64 st; 
  CHECK_NULL (format);
  mformat = *format;

  if (fp_data)
    fclose (fp_data);
  if (fp_meta)
    fclose (fp_meta);

  if (format->input_file) {
    fp_data = fopen(format->input_file, "r");
    if (!fp_data) {
      LOG_ERROR ("open data file fail: %s", format->input_file);
      return false;
    }
  }

  file_size = 0;
  if (stat64(format->input_file, &st) == 0) {
    file_size = st.st_size;
    LOG_DEBUG ("data file size: %lld\n", file_size);
  }

  if (format->input_meta) {
    int size, key;
    int64 pts, dts, offset;
    static char str [8];
    fp_meta = fopen(format->input_meta, "r");
    if (!fp_meta) {
      LOG_ERROR ("open meta file fail: %s", format->input_meta);
      return false;
    }
    fscanf (fp_meta, "format: %s width: %d height: %d fps_n: %d fps_d: %d\n",
        str, &mformat.width, &mformat.height, &mformat.fps_n,
        &mformat.fps_d);
    mformat.format = str;
    if (str) {
      char *pch = str;
      while (*pch != '\0') {
        *pch = toupper (*pch);
        pch++; 
      }   
    }

    while (fscanf (fp_meta, "PTS: %lld DTS: %lld offset: %lld length: %d key frame: %d\n",
          &pts, &dts, &offset, &size, &key) >= 0) {
      if (pts > duration)
        duration = pts;
    };
    LOG_INFO ("duration: %lld\n", duration);
    fseek(fp_meta, 0, SEEK_SET);
    fscanf (fp_meta, "format: %s width: %d height: %d fps_n: %d fps_d: %d\n",
        str, &mformat.width, &mformat.height, &mformat.fps_n,
        &mformat.fps_d);
  } else {
    if (format->format == NULL) {
      char *subfix;
      subfix = strrchr (format->input_file, '.');
      if (subfix) {
        char *pch = subfix;
        while (*pch != '\0') {
          *pch = toupper (*pch);
          pch++; 
        }   
        mformat.format = subfix + 1;
      }
    }
    if (format->width == 0 && format->height == 0) {
      int i1, i2;
      if (2 == sscanf(format->input_file, "%*[^0123456789]%d%*[^0123456789]%d",
            &i1, &i2)) {
        mformat.width = i1;
        mformat.height = i2;
      }
    }
    if (format->fps_n == 0 && format->fps_d == 0) {
        mformat.fps_n = 30;
        mformat.fps_d = 1;
    }
  }
  LOG_INFO ("format: %s width: %d height: %d fps_n: %d fps_d: %d\n",
        mformat.format, mformat.width, mformat.height, mformat.fps_n,
        mformat.fps_d);
  return true;
}

bool file_input::get_format (Format *format)
{
  CHECK_NULL (format);
  *format = mformat;
  return true;
}

bool file_input::get_buffer (Buffer * buf)
{
  int i, size, key;
  int64 pts, dts, offset;
  CHECK_NULL (buf);
  for (i = 0; i < buf->plane_num; i ++) {
    if (buf->data[i] == NULL) {
      buf->data[i] = (char *)malloc (buf->alloc_size[i]);
      if (buf->data[i] == NULL) {
        LOG_ERROR ("malloc fail: %d\n", errno);
        return false;
      }
    }
    size = buf->alloc_size[i];
    if(bSeek){
        fseek(fp_data, seek_offset, SEEK_SET);
        size = seek_size;
        buf->pts = seek_pts/1000000;
        buf->dts = seek_dts/1000000;
        bSeek = false;
    }
    else if (fp_meta) {
      if (fscanf (fp_meta, "PTS: %lld DTS: %lld offset: %lld length: %d key frame: %d\n",
            &pts, &dts, &offset, &size, &key) > 0) {
        LOG_DEBUG ("input buffer offset: %lld size: %d, pts: %lld dts: %lld\n",
            offset, size, pts, dts);
        if (bfirst_buffer) {
          size += offset;
          offset = 0;
          bfirst_buffer = false;
        }
        buf->pts = pts/1000000;
        buf->dts = dts/1000000;
        fseek(fp_data, offset, SEEK_SET);
      } else {
        size = 0;
    }
    }
    buf->size[i] = fread((void*)buf->data[i], 1, size, fp_data);
    if (buf->size[i] < 0) {
      LOG_ERROR ("fread fail: %d\n", errno);
      return false;
    }
    LOG_DEBUG ("input buffer size: %d, pts: %lld dts: %lld\n", buf->size[i],
        buf->pts, buf->dts);
  }
  return true;
}

bool file_input::set_position (int portion, int64 timems)
{
  int64 position, pts, dts;
  int size, key;
  char str [8];
  if (fp_meta) {
    if (timems != INVALID_PARAM) {
    } else if (portion != INVALID_PARAM) {
      timems = duration * portion / 100 / 1000000;
    }
    fseek(fp_meta, 0, SEEK_SET);
    fscanf (fp_meta, "format: %s width: %d height: %d fps_n: %d fps_d: %d\n",
        str, &mformat.width, &mformat.height, &mformat.fps_n,
        &mformat.fps_d);

    if(timems == 0)
        return true;

    do {
      if (fscanf (fp_meta, "PTS: %lld DTS: %lld offset: %lld length: %d key frame: %d\n",
            &pts, &dts, &position, &size, &key) < 0)
        break;
      if((pts/1000000 >= timems) && key == 1)
        break;
    } while (1);
    bSeek = true;
    seek_offset = position;
    seek_size = size;
    seek_pts = pts;
    seek_dts = dts;
    LOG_INFO ("seek to pts: %lld dts: %lld's next point\n", pts, dts);
  } else if (portion != INVALID_PARAM) {
    position = file_size * portion / 100;
    LOG_INFO ("seek file to: %lld\n", position);
    fseek(fp_data, position, SEEK_SET);
  }
  return true;
}
