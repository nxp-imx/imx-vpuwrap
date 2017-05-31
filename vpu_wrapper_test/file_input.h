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

#ifndef __FILE_INPUT_H__
#define __FILE_INPUT_H__

#include <stdio.h>
#include "input.h"
#include "common.h"

class file_input : public input {
  public:
    file_input ();
    virtual ~file_input ();
    bool set_format (Format *format);
    bool get_format (Format *format);
    bool get_buffer (Buffer *buf);
    bool set_position (int portion, int64 timems);
  private:
    FILE *fp_data, *fp_meta;
    Format mformat;
    int64 file_size;
    int64 duration;
    bool bfirst_buffer;
    bool bSeek;
    int64 seek_offset;
    int seek_size;
    int64 seek_pts;
    int64 seek_dts;
};

#endif
