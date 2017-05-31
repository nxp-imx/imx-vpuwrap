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

#ifndef __SINK_H__
#define __SINK_H__

#include "process.h"
#include "common.h"

class sink {
  public:
    sink ();
    virtual ~sink ();
    bool set_format (Format *format);
    virtual bool alloc_buffer (Buffer *buf);
    bool put_buffer (Buffer *buf);
    virtual int64 get_mediatime ();
    virtual bool flush ();
    virtual bool free_buffer (Buffer *buf);
    virtual Buffer *get_render_buffer ();
    virtual bool put_buffer_sink (Buffer *buf);
  private:
    virtual bool set_format_sink (Format *format) = 0;
    Format mformat;
    process *pprocess;
};

#endif
