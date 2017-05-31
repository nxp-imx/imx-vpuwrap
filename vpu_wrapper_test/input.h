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

#ifndef __INPUT_H__
#define __INPUT_H__

#include "common.h"

class input {
  public:
    input () {};
    virtual ~input () {};
    virtual bool set_format (Format *format) = 0;
    virtual bool get_format (Format *format) = 0;
    virtual bool get_buffer (Buffer *buf) = 0;
    virtual bool set_position (int portion, int64 timems);
  private:
    Format mformat;
};

#endif
