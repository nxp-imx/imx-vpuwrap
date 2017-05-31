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

#include "fb_sink.h"
#include "file_sink.h"
#include "output.h"
#include "log.h"

typedef enum {
  KMS_SINK,
  FILE_SINK
} SINK_MODE;

output::output ()
{
  panalysis = new analysis ();
  psink = NULL;
}

output::~output ()
{
  panalysis->get_result ();
  delete panalysis;
  if (psink)
    delete psink;
}

bool output::set_format (Format *format)
{
  CHECK_NULL (format);
  if (psink)
    delete psink;
  if (format->output_mode == FILE_SINK)
    psink = new file_sink ();
  else
    psink = new fb_sink ();
  panalysis->set_format (format);
  psink->set_format (format);
  return true;
}

bool output::alloc_buffer (Buffer *buf)
{
  return psink->alloc_buffer (buf);
}

bool output::put_buffer (Buffer *buf)
{
  CHECK_NULL (buf);
  panalysis->put_buffer (buf);
  psink->put_buffer (buf);
  return true;
}

int64 output::get_mediatime ()
{
  return psink->get_mediatime ();
}

bool output::flush ()
{
  return psink->flush ();
}

bool output::free_buffer (Buffer *buf)
{
  return psink->free_buffer (buf);
}
