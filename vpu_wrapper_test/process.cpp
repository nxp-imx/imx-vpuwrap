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

#include <errno.h>
#include <string.h>
#include "process.h"
#include "log.h"

#define MIN(a,b) ((a)<(b) ? (a) : (b))

process::process ()
{
  if(g2d_open(&g2d_handle) == -1 || g2d_handle == NULL) {
    LOG_ERROR ("Failed to open g2d device.");
  }
  use_my_memory = false;
}

process::~process ()
{
  g2d_close (g2d_handle);
}

bool process::set_format (Format *format)
{
  CHECK_NULL (format);
  mformat = *format;
  int i, j;

  src.format = G2D_NV12;
  src.global_alpha = 0xff;
  src.left = format->crop_left;
  src.top = format->crop_top;
  src.right = format->crop_left + MIN(format->crop_width, format->width-format->crop_left);
  src.bottom = format->crop_top + MIN(format->crop_height, format->height-format->crop_top);
  src.width = format->width;
  src.height = format->height;
  src.stride = format->width;
  dst.format = G2D_ARGB8888;
  dst.global_alpha = 0xff;
  dst.width = 1920;
  dst.stride = 1936;
  dst.height = 1080;
  dst.left = 0;
  dst.top = 0;
  dst.right = 1920;
  dst.bottom = 1080;

  for (i = 0; i < MAX_V4L2_BUFFERS; i ++) {
    for (j = 0; j < MAX_V4L2_PLANES; j++) {
      pbuf[i][j] = NULL;
    }
  }

  return true;
}

int process::find_null ()
{
  int i, j;
  for (i = 0; i < MAX_V4L2_BUFFERS; i ++) {
    for (j = 0; j < mformat.plane_num; j++) {
      if (pbuf[i][j] == NULL)
        return i;
    }
  }

  return MAX_V4L2_BUFFERS;
}

int process::find_buf (char *buf)
{
  int i, j;
  for (i = 0; i < MAX_V4L2_BUFFERS; i ++) {
    for (j = 0; j < mformat.plane_num; j++) {
      if (pbuf[i][j]->buf_vaddr == buf)
        return i;
    }
  }

  return MAX_V4L2_BUFFERS;
}

bool process::put_buffer (Buffer *buf, Buffer *render)
{
  int size, i, j=0;
  CHECK_NULL (buf);

  for (i = 0; i < mformat.plane_num; i ++) {
    if (buf->data[i] == NULL) {
      j = find_null ();
      if (j < MAX_V4L2_BUFFERS) {
        pbuf[j][i] = g2d_alloc (buf->alloc_size[i], 0);
        if (!pbuf[j][i]) {
          LOG_ERROR("G2D allocate %u bytes memory failed: %s",
              buf->alloc_size[i], strerror(errno));
          return false;
        }
      } else
        LOG_ERROR ("can't find null buffer.\n");

      LOG_DEBUG ("Use process memory.\n");
      buf->data[i] = (char *) pbuf[j][i]->buf_vaddr;
      if (buf->data[i] == NULL) {
        LOG_ERROR ("malloc fail: %d\n", errno);
        return false;
      }
      use_my_memory = true;
    }
  }

  if (render && buf->size[0] > 0) {
    for (i = 0; i < mformat.plane_num; i ++) {
      if (use_my_memory == false) {
        if (pbuf[0][i] == NULL) {
          pbuf[0][i] = g2d_alloc (buf->alloc_size[i], 0);
          if (!pbuf[0][i]) {
            LOG_ERROR("G2D allocate %u bytes memory failed: %s",
                buf->alloc_size[i], strerror(errno));
            return false;
          }
        }
        memcpy (pbuf[0][i]->buf_vaddr, buf->data[i], buf->size[i]);
        src.planes[i] = pbuf[0][i]->buf_paddr;
      } else {
        j = find_buf (buf->data[i]);
        if (j < MAX_V4L2_BUFFERS) {
          src.planes[i] = pbuf[j][i]->buf_paddr;
        } else
          LOG_ERROR ("can't find buffer.\n");
      }
    }
    src.planes[2] = (unsigned int)src.planes[1]+(unsigned int)(buf->size[1]>>1);
    dst.planes[0] = (long)render->data[0];

    g2d_blit(g2d_handle, &src, &dst);

    g2d_finish(g2d_handle);
  }

  return true;
}
