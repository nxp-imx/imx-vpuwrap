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

#ifndef __OUTPUT_H__
#define __OUTPUT_H__

#include "analysis.h"
#include "sink.h"
#include "common.h"

class output {
  public:
    output ();
    virtual ~output ();
    bool set_format (Format *format);
    bool alloc_buffer (Buffer *buf);
    bool put_buffer (Buffer *buf);
    int64 get_mediatime ();
    bool flush ();
    bool free_buffer (Buffer *buf);
  private:
    analysis *panalysis;
    sink *psink;
};

#endif
