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
#include "sink.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
sink::sink ()
{
  pprocess = NULL;
}

sink::~sink ()
{
  if(pprocess)
    delete pprocess;
}

bool sink::set_format (Format *format)
{
  CHECK_NULL (format);
  mformat = *format;
  //no process function for gl sink
  if(GL_SINK != mformat.output_mode){
    if(pprocess == NULL)
        pprocess = new process ();
    pprocess->set_format (format);
  }
  set_format_sink (format);
  return true;
}

bool sink::alloc_buffer (Buffer *buf)
{
  int i;
  for (i = 0; i < mformat.plane_num; i ++) {
    buf->data[i] = (char *)malloc (mformat.image_size[i]);
    buf->alloc_size[i] = mformat.image_size[i];
  }
  return true;
}

bool sink::put_buffer (Buffer * buf)
{
  CHECK_NULL (buf);
  if(GL_SINK == mformat.output_mode){
    put_buffer_sink (buf);
  }else{
    pprocess->put_buffer (buf, get_render_buffer());
    put_buffer_sink (buf);
  }
  return true;
}

Buffer *sink::get_render_buffer ()
{
  return NULL;
}

bool sink::put_buffer_sink (Buffer *buf)
{
  return true;
}

int64 sink::get_mediatime ()
{
  return INVALID_TIME;
}

bool sink::flush ()
{
  return true;
}

bool sink::free_buffer (Buffer * buf)
{
  return true;
}
