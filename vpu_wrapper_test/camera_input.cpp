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

#include "camera_input.h"
#include "log.h"

bool camera_input::set_format (Format *format)
{
  CHECK_NULL (format);
  mformat = *format;
  return true;
}

bool camera_input::get_format (Format *format)
{
  CHECK_NULL (format);
  *format = mformat;
  return true;
}

bool camera_input::get_buffer (Buffer *buf)
{
  CHECK_NULL (buf);
  return true;
}

