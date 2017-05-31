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

#ifndef __PROCESS_H__
#define __PROCESS_H__

#include "g2d.h"
#include "common.h"

class process {
  public:
    process ();
    virtual ~process ();
    bool set_format (Format *format);
    bool put_buffer (Buffer *buf, Buffer *render_buffer);
  private:
    int find_null ();
    int find_buf (char *buf);
    Format mformat;
    void *g2d_handle;
    struct g2d_buf *pbuf[MAX_V4L2_BUFFERS][MAX_V4L2_PLANES];
    struct g2d_surface src;
    struct g2d_surface dst;
    bool use_my_memory;
};

#endif
