/*
 * Copyright 2016 Freescale Semiconductor, Inc. All rights reserved.
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
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/fb.h>
#include "fb_sink.h"
#include "log.h"

fb_sink::fb_sink ()
{
  fd = -1;
}

fb_sink::~fb_sink ()
{
  if (fd >= 0)
    close (fd);
}

bool fb_sink::set_format_sink (Format *format)
{
  CHECK_NULL (format);
  mformat = *format;
  struct fb_fix_screeninfo fb_fix;
  struct fb_var_screeninfo fb_var;
  char *device="/dev/fb4";

  if (fd >= 0)
    return true;

  fd = open (device, O_RDWR, 0);
  if (fd < 0) {
    LOG_ERROR ("Can't open %s.", device);
    return false;
  }

  if (ioctl (fd, FBIOGET_VSCREENINFO, &fb_var) < 0) {
    LOG_ERROR ("FBIOGET_VSCREENINFO from %s failed.", device);
    close (fd);
    return false;
  }

  if (ioctl(fd, FBIOGET_FSCREENINFO, &fb_fix) < 0) {
    LOG_ERROR ("FBIOGET_FSCREENINFO from %s failed.", device);
    close (fd);
    return false;
  }

  render_buffer.data[0] = (char *)fb_fix.smem_start;

  return true;
}

Buffer *fb_sink::get_render_buffer ()
{
  return &render_buffer;
}


