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

#ifndef __FILE_SINK_H__
#define __FILE_SINK_H__

#include <stdio.h>
#include "sink.h"
#include "common.h"

class file_sink : public sink {
  public:
    file_sink ();
    virtual ~file_sink ();
    bool set_format_sink (Format *format);
    bool put_buffer_sink (Buffer *buf);
  private:
    FILE *fp_data, *fp_meta;
    Format mformat;
    int write_size;
};

#endif
