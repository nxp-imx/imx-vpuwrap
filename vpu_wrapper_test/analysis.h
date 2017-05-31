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

#ifndef __ANALYSIS_H__
#define __ANALYSIS_H__

#include <stdio.h>
#include <time.h>
#include "common.h"

class analysis {
  public:
    analysis ();
    virtual ~analysis ();
    bool set_format (Format *format);
    bool put_buffer (Buffer *buf);
    bool get_result ();
  private:
    FILE *fp_data, *fp_meta;
    Format mformat;
    struct timespec start;
    struct timespec pre_time;
    int frame_count;
};

#endif
