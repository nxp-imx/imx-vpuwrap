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

#ifndef __FB_SINK_H__
#define __FB_SINK_H__

#include "sink.h"
#include "common.h"

class fb_sink : public sink {
  public:
    fb_sink ();
    virtual ~fb_sink ();
    bool set_format_sink (Format *format);
    Buffer *get_render_buffer ();
  private:
    int fd;
    Format mformat;
    Buffer render_buffer;
};

#endif
