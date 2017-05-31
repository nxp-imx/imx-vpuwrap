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

#ifndef __CAMERA_INPUT_H__
#define __CAMERA_INPUT_H__

#include "input.h"
#include "common.h"

class camera_input : public input {
  public:
    camera_input () {};
    virtual ~camera_input () {};
    bool set_format (Format *format);
    bool get_format (Format *format);
    bool get_buffer (Buffer *buf);
  private:
    Format mformat;
};

#endif
